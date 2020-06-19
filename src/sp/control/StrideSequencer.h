/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2020 Roy Spliet, University of Cambridge
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef SP_CONTROL_STRIDESEQUENCER_H
#define SP_CONTROL_STRIDESEQUENCER_H

#include <cstdint>
#include <array>
#include <systemc>

#include "model/Register.h"
#include "model/stride_descriptor.h"
#include "sp/model/DQ_reservation.h"
#include "util/debug_output.h"
#include "util/sched_opts.h"

using namespace sc_core;
using namespace sc_dt;
using namespace sp_model;
using namespace simd_model;

namespace sp_control {

/**
 * Convert a descriptor to a stream of StorageArray commands and DQ control
 * signals.
 * @param BUS_WIDTH Number of 32-bit words in a burst.
 * @param THREADS Number of threads in a work-group.
 */
template <unsigned int WG, unsigned int BUS_WIDTH, unsigned int THREADS>
class StrideSequencer : public sc_module
{
private:
	/** Look-up table for increment values for small periods, such that
	 * no more than a single overflow occurs. */
	unsigned int increment_lut[BUS_WIDTH];

	/** Another lookup table for small period "line" increments. */
	unsigned int line_increment_lut[BUS_WIDTH];

	/** Phase of each word-lane wrt period. */
	sc_uint<22> phase[BUS_WIDTH];

	/** Period number we're in */
	sc_int<32> line[BUS_WIDTH];

	/** Current descriptor register. */
	stride_descriptor desc;

	/** Address iterator, runs from in_addr to end_addr. */
	sc_uint<32> global_addr;

	/** Local address iterator, runs from dst_offset. Provides indexes
	 * (offsets in a given buffer) for CAM transfers. */
	sc_uint<32> local_idx;

	/** in_addr + (in_period * in_period_count). */
	sc_uint<32> end_addr;

	/** When the period is much larger than the word count, we can skip over
	 * regions that will contain just zeroes. skip contains the minimum
	 * size of such a region, aligned to BUS_WIDTH words. Can be negative
	 * -BUS_WIDTH if skips shouldn't occur
	 * (e.g. when period - words < BUS_WIDTH). */
	int skip;

	/** In some special cases, we can skip over one full line extra. Cache
	 * the sum to reduce critical path length. */
	unsigned int skip_bw;

	/** This value will help determine which the special cases are for which
	 * we can skip over skip_bw words rather than just skip. */
	unsigned int skip_rest;

	/** The base increment of line on every cycle, 0 for any period larger
	 * than BUS_WIDTH. */
	unsigned int line_increment;

	/** State of the command generator. */
	enum {
		DQ_ST_IDLE = 0,
		DQ_ST_FETCH,
		DQ_ST_INIT_STATE,
		DQ_ST_RUNNING,
		DQ_ST_WAIT_DONE
	} state = DQ_ST_IDLE;

public:
	/** DRAM clock, SDR. */
	sc_in<bool> in_clk{"in_clk"};

	/** FIFO of descriptors.
	 * @todo Depth? */
	sc_fifo_in<stride_descriptor> in_desc_fifo{"in_desc_fifo"};

	/** Trigger translation of top FIFO item. */
	sc_fifo_in<bool> in_trigger{"in_trigger"};

	/** Ready to accept next descriptor. FIFO to handle crossing clock
	 * domains. */
	sc_fifo_out<sc_uint<1> > out_wg_done{"out_wg_done"};

	/** DQ finished the last transfer. */
	sc_in<bool> in_dq_done{"in_dq_done"};

	/** Generated burst-sized requests. */
	sc_fifo_out<sp_model::DQ_reservation<BUS_WIDTH,THREADS> >
			out_dq_fifo{"out_dq_fifo"};

	/** Register addressed by DRAM, if any. */
	sc_inout<AbstractRegister> out_rf_reg_w{"out_rf_reg_w"};

	/** True iff this stride descriptor is a write.
	 * Stuck in a separate signal because DQ must function as a pipeline. */
	sc_inout<bool> out_write{"out_write"};

	/** Scheduling options. */
	sc_in<sc_bv<WSS_SENTINEL> > in_sched_opts{"in_sched_opts"};

	/** Ticket number that's ready to pop. */
	sc_in<sc_uint<4> > in_ticket_pop{"in_ticket_pop"};

	/** Construct thread, initialise LUT values. */
	SC_CTOR(StrideSequencer) : skip(0), skip_bw(0), skip_rest(0)
	{
		unsigned int i;
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();

		increment_lut[0] = 0;
		for (i = 1; i < BUS_WIDTH; i++) {
			increment_lut[i] = BUS_WIDTH % i;
			line_increment_lut[i] = (BUS_WIDTH - 1) / i;
		}
	}

private:
	/** Modulo operation (mod desc.period) for situations in which
	 * increment is guaranteed to only overflow cur_phase once.
	 * @param cur_phase Phase of this word line.
	 * @param increment Value to increment the current phase with.
	 * @param overflew Pointer to a boolean to set the overflow bit.
	 * @return The new phase value.
	 */
	sc_uint<22>
	single_overflow_modulo(sc_uint<22> cur_phase, sc_uint<22> increment,
			bool *overflew = nullptr)
	{
		sc_uint<22> out = cur_phase + increment;
		if (out >= desc.period) {
			if (overflew != nullptr)
				*overflew = true;
			out -= desc.period;
		} else {
			if (overflew != nullptr)
				*overflew = false;
		}

		return out;
	}

	/** For given word lane, determine whether this word needs to be
	 * transferred
	 * @param lane Word lane
	 * @return True iff this word must be read, false otherwise.
	 */
	bool
	word_mask_select(unsigned int lane)
	{
		sc_uint<32> addr;

		addr = global_addr + (lane << 2);

		if (phase[lane] < desc.words && end_addr > addr &&
				desc.addr <= addr)
			return true;

		return false;
	}

	/** When the phase exceeds the number of words, increment global_address
	 * such that we skip over all addresses that generate a word mask of 0.
	 * @param phase Phase value for the last word lane.
	 * @return Number of words the global address should be incremented
	 * with.
	 */
	sc_uint<22>
	address_increment(sc_uint<22> phase)
	{
		if (phase < (desc.words - 1) || desc.period < BUS_WIDTH)
			return BUS_WIDTH;

		if (phase < skip_rest)
			return skip_bw + BUS_WIDTH;

		return skip + BUS_WIDTH;
	}

	/** For given address increment, find the accompanying phase increment.
	 * When the period is small, look up the right value in the LUT,
	 * otherwise perform a single overflow modulo to bound the phase
	 * increment to period.
	 * @param addr_increment The calculated address increment.
	 * @return Number of words the phase for each lane should be incremented
	 * with.
	 */
	sc_uint<22>
	phase_increment(sc_uint<22> addr_increment)
	{
		if (desc.period < BUS_WIDTH)
			return increment_lut[desc.period];

		return single_overflow_modulo(0, addr_increment);
	}

	/** Check preconditions of the stride descriptor.
	 *
	 * Since these errors tend to be user errors, throw an exception when
	 * any precondition is violated.
	 */
	void
	validate_sd(stride_descriptor &d)
	{
		if (d.period == 0)
			throw invalid_argument("Period must be larger than 0.");

		switch (d.dst.type) {
		case TARGET_REG:
			if (!is_pot(d.dst_period))
				throw invalid_argument("Destination period must be "
					"power-of-two when targeting (vector) register file.");
			break;
		case TARGET_SP:
			throw invalid_argument("Scratchpad-to-Scratchpad transfers "
					"are unsupported.");
		default:
			break;
		}
		return;
	}

	/** Given a descriptor, initialise some internal values used for
	 * iterators or to break long paths
	 */
	void
	init_request_regs(void)
	{
		unsigned int i;
		unsigned int it;
		int l;

		try {
			validate_sd(desc);
		} catch (invalid_argument &e) {
			cerr << e.what() << endl;
			throw;
		}

		skip = desc.period - (desc.words + (BUS_WIDTH - 1));
		skip_rest = (skip & (BUS_WIDTH-1)) + desc.words - 1;

		skip &= ~(BUS_WIDTH-1);
		skip_bw = skip + BUS_WIDTH;

		/** @todo I don't think we'd want a multiplier here in a
		 * real arch. Can we re-use the mul of the compute unit
		 * connected to this DRAM controller? */
		end_addr = desc.addr +
		    ((desc.words + (desc.period * (desc.period_count - 1))) << 2);
		global_addr = desc.addr;
		local_idx = desc.dst_offset;

		l = desc.dst_off_y;
		desc.dst_offset = desc.dst_off_x;

		if (desc.period < BUS_WIDTH)
			line_increment = line_increment_lut[desc.period];
		else
			line_increment = 0;

		/** @todo Likewise, a real modulo is way too expensive. I
		 * suspect we might end up with a small LUT and a
		 * single-overflow-modulo for this */
		it = 0;
		for (i = 0; i < BUS_WIDTH; i++) {
			phase[i] = it;
			line[i] = l;

			it = (it + 1);
			if ((it % desc.period) != it) {
				l++;
				it = it % desc.period;
			}
		}
	}

	/** Debug: Print stride descriptor and cycle time upon completion.
	 * @param sd Stride descriptor that was processed.
	 * @param cycles Number of cycles at the DRAM command bus clock that
	 * 		 this request took. */
	inline void
	debug_print_fe(stride_descriptor sd, unsigned long cycles)
	{
		if (debug_output[DEBUG_MEM_FE])
			cout << sd << " " << cycles << " cycles" << endl;
	}

	/** Translate a StrideSequencer lane ID to a register offset ID.
	 * @param t Destination type.
	 * @param i Index of StrideSequencer lane
	 * @return Register offset ID. */
	inline reg_offset_t<THREADS>
	regIdx(req_dest_type_t t, unsigned int i)
	{
		unsigned int phase_shift;
		unsigned int phase_mask;
		unsigned int p;
		unsigned int lane;
		unsigned int row;

		if (t == TARGET_CAM) {
			return reg_offset_t<THREADS>(local_idx + i);
		} else {
			phase_shift = int(desc.idx_transform);
			phase_mask = (1 << int(desc.idx_transform)) - 1;
			p = phase[i] + desc.dst_offset;

			lane = (line[i] * desc.dst_period) | (p >> phase_shift);
			row = p & phase_mask;

			return reg_offset_t<THREADS>(lane, row);
		}
	}

	/** Main thread */
	void
	thread_lt(void)
	{
		unsigned int i;
		sc_uint<22> ph_inc;
		sc_uint<22> addr_inc;
		sp_model::DQ_reservation<BUS_WIDTH,THREADS> req;
		unsigned int words;
		AbstractRegister *reg;
		bool overflew;
		stride_descriptor d;

		sc_bv<BUS_WIDTH> wm;
		array<reg_offset_t<THREADS>,BUS_WIDTH> ridx;

		sc_uint<1> wg;

		while (true) {
			words = 0;

			switch (state) {
			case DQ_ST_IDLE:
				/* Blocking FIFO. */
				in_trigger.read();

				state = DQ_ST_FETCH;
				/* fall-through */
			case DQ_ST_FETCH:
				if (!in_desc_fifo.num_available()) {
					if (out_dq_fifo.num_free() == 1) {
						state = DQ_ST_IDLE;
					}

					break;
				}

				state = DQ_ST_INIT_STATE;
				in_desc_fifo.read(desc);
				/* fall-through */
			case DQ_ST_INIT_STATE:
				if (in_sched_opts.read()[WSS_NO_PARALLEL_DRAM_SP] &&
				    in_ticket_pop.read() != desc.ticket)
					break;

				state = DQ_ST_RUNNING;
				d = desc;
				req.sp_offset = 0;
				req.rw = true;

				assert(desc.getTargetType() != TARGET_SP);

				reg = desc.getTargetReg();
				wg = reg->wg;
				out_rf_reg_w.write(*reg);
				delete reg;
				reg = nullptr;

				out_write.write(!d.write);

				init_request_regs();
				break;
			case DQ_ST_RUNNING:
				for (i = 0; i < BUS_WIDTH; i++) {
					wm[i] = word_mask_select(i);
					if (wm[i]) {
						words++;
						ridx[i] = regIdx(desc.getTargetType(), i);
					}
				}

				addr_inc = address_increment(phase[BUS_WIDTH - 1]);
				ph_inc = phase_increment(addr_inc);

				for (i = 0; i < BUS_WIDTH; i++) {
					line[i] += line_increment;
					phase[i] = single_overflow_modulo(
							phase[i], ph_inc,
							&overflew);
					if (overflew || ph_inc == 0)
						line[i]++;
				}

				req = sp_model::DQ_reservation<BUS_WIDTH,THREADS>(
						global_addr,wm,desc.write,
						desc.getTargetType(),ridx);

				global_addr += (addr_inc << 2);
				local_idx += addr_inc;

				if (global_addr >= end_addr) {
					req.last = true;
					out_dq_fifo.write(req);
					state = DQ_ST_WAIT_DONE;
				} else {
					req.last = false;
					out_dq_fifo.write(req);
				}
				break;
			case DQ_ST_WAIT_DONE:
				if (in_dq_done.read()) {
					state = DQ_ST_FETCH;
					out_wg_done.write(WG);
					out_rf_reg_w.write(AbstractRegister());
					//debug_print_fe(d, cycle_end - cycle_start);
				}
				break;
			}

			wait();
		}
	}
};

}

#endif /* SP_CONTROL_STRIDESEQUENCER_H */
