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

#ifndef MC_CONTROL_STRIDESEQUENCER_H
#define MC_CONTROL_STRIDESEQUENCER_H

#include <cstdint>
#include <array>
#include <systemc>

#include "model/Register.h"
#include "model/stride_descriptor.h"
#include "mc/model/burst_request.h"
#include "util/debug_output.h"
#include "util/defaults.h"
#include "util/sched_opts.h"

using namespace sc_core;
using namespace sc_dt;
using namespace mc_model;
using namespace simd_model;
using namespace std;

namespace mc_control {

/**
 * Convert a large DRAM request (1D/2D strides or iterative indexed) to a stream
 * of DRAM commands.
 * @image html mc/StrideSequencer.png
 * @param BUS_WIDTH Number of 32-bit words in a burst.
 * @todo This component should really be called FrontEnd, as it also contains
 * 	 the IndexIterator submodule code.
 */
template <unsigned int BUS_WIDTH,
	unsigned int THREADS = COMPUTE_THREADS, unsigned int LANES = COMPUTE_FPUS>
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

	/** Scratchpad address increment when advancing to the next period. */
	unsigned int sp_line_addr_increment;

	/** First cycle of execution for the current request. */
	unsigned long cycle_start;

	/** Last cycle this request is executing. */
	unsigned long cycle_end;

	/** State of the command generator.
	 *
	 * The front-end is designed as a state machine, with init, run, drain
	 * phases. The current state is stored in this "register". */
	enum {
		CMDGEN_ST_IDLE = 0,
		CMDGEN_ST_FETCH,
		CMDGEN_ST_INIT_STATE,
		CMDGEN_ST_RUNNING_STRIDE,
		CMDGEN_ST_RUNNING_IDXIT,
		CMDGEN_ST_WAIT_ALLPRE
	} state = CMDGEN_ST_IDLE;

public:
	/** DRAM clock, SDR. */
	sc_in<bool> in_clk{"in_clk"};

	/** FIFO of descriptors.
	 * @todo Depth? */
	sc_fifo_in<stride_descriptor> in_desc_fifo{"in_desc_fifo"};

	/** Trigger translation of top FIFO item. */
	sc_fifo_in<bool> in_trigger{"in_trigger"};

	/** Is a refresh in progress or pending? */
	sc_in<bool> in_ref_pending{"in_ref_pending"};

	/** Generated burst-sized requests. */
	sc_fifo_out<burst_request<BUS_WIDTH,THREADS> > out_req_fifo{"out_req_fifo"};

	/** Ready to accept next descriptor. */
	sc_inout<bool> out_done{"out_done"};

	/** Signal indicating that all reads for the current active stride were
	 * finished, and all banks are precharged, e.g. ready for the next
	 * request */
	sc_in<bool> in_DQ_allpre{"in_DQ_allpre"};

	/** Which destination is targeted by the currently active request? */
	sc_inout<RequestTarget> out_dst{"out_dst"};

	/** Register addressed by DRAM, if any. */
	sc_inout<AbstractRegister> out_dst_reg{"out_dst_reg"};

	/** Trigger the start of pushing indexes from RF. */
	sc_inout<bool> out_idx_push_trigger{"out_idx_push_trigger"};

	/** RF will start pushing indexes for "index iteration" transfers. */
	sc_fifo_in<idx_t<THREADS> > in_idx{"in_idx"};

	/** Cycle counter. Shared with Backend. */
	sc_in<long> in_cycle{"in_cycle"};

	/** Scheduling options. */
	sc_in<sc_bv<WSS_SENTINEL> > in_sched_opts{"in_sched_opts"};

	/** Ticket number that's ready to pop. */
	sc_in<sc_uint<4> > in_ticket_pop{"in_ticket_pop"};

	/** Construct thread, initialise LUT values. */
	SC_CTOR(StrideSequencer) : skip(0), skip_bw(0), skip_rest(0),
			cycle_start(0ul), cycle_end(0ul)
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
	 * @param overflew Pointer to a bool where the overflow bit can be
	 * 		   stored.
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
	 * @return True iff this word must be read, false otherwise. */
	bool
	word_mask_select(unsigned int lane)
	{
		sc_uint<32> addr;

		addr = global_addr | (lane << 2);

		if (phase[lane] < desc.words && end_addr > addr &&
				desc.addr <= addr)
			return true;

		return false;
	}

	/** When the phase exceeds the number of words, increment global_address
	 * such that we skip over all addresses that generate a word mask of 0.
	 * @param phase Phase value for the last word lane.
	 * @return Number of words the global address should be incremented
	 * with. */
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
	 * with. */
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
	 * @param d Stride descriptor to validate. */
	void
	validate_sd(stride_descriptor &d)
	{
		if (d.period == 0)
			throw invalid_argument("Period must be larger than 0.");

		switch (d.dst.type) {
		case TARGET_REG:
			if (!is_pot(d.dst_period))
				throw invalid_argument("Destination period must be "
					"power-of-two when targeting (vector) register file");
			break;
		case TARGET_SP:
			if (d.words != d.dst_period && d.period < d.words + BUS_WIDTH)
				throw invalid_argument("Non-contiguous writes to scratchpad "
					"period n+1 of at least " + to_string(BUS_WIDTH) + " words.");
			break;
		default:
			break;
		}
		return;
	}

	/** Given a descriptor in the "global" descriptor "desc", initialise
	 * the internal values used for iterators or to break long paths. */
	void
	init_request_regs(void)
	{
		unsigned int i;
		unsigned int word;
		unsigned int it;
		int l;
		int addr_diff;

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
		global_addr = desc.addr & ~((BUS_WIDTH << 2) - 1);
		addr_diff = global_addr - desc.addr;
		local_idx = desc.dst_offset + (addr_diff >> 2);

		/* Round-up division. We negate l two lines down. For
		 * desc.period >= BUS_WIDTH, l should always represent 0 or -1.
		 * We probably don't need a full divider for this, we can get
		 * away with the much used single-modulo-divider and a LUT.
		 * If not, move division to the compute resource in a real
		 * implementation. */
		l = ((-addr_diff >> 2) + (desc.period.to_int() - 1))
				/ desc.period.to_int();
		l = desc.dst_off_y - l;

		if (desc.period < BUS_WIDTH)
			line_increment = line_increment_lut[desc.period];
		else
			line_increment = 0;

		if (desc.dst.type == TARGET_SP && desc.dst_period >= desc.words)
			sp_line_addr_increment = (desc.dst_period - desc.words) << 2;
		else
			sp_line_addr_increment = 0;

		/** @todo Likewise, a real modulo is way too expensive. A
		 * hardware implementation would use a small LUT and a
		 * single-overflow-modulo for initialising the line and phase
		 * values. */
		word = (desc.addr >> 2) & (BUS_WIDTH - 1);
		it = (desc.period - word) % desc.period;
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

	/** Debug: print the time a stride request took to finish to stdout.
	 * @param sd Stride descriptor to print
	 * @param cycles Number of cycles, calculated from the cycles_end
	 * 		 and cycles_start state. */
	inline void
	debug_print_fe(stride_descriptor sd, unsigned long cycles)
	{
		if (debug_output[DEBUG_MEM_FE])
			cout << sd << " " << cycles << " cycles" << endl;
	}

	/** Translate a StrideSequencer lane ID to a register offset ID.
	 * @param t Register type (CAM or regular VGPR)
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
			p = phase[i] + desc.dst_off_x;

			/* dst_period guaranteed power-of-two. */
			lane = (line[i] * desc.dst_period) | (p >> phase_shift);
			row = p & phase_mask;

			return reg_offset_t<THREADS>(lane, row);
		}
	}

	/** Forward the target register from the stride descriptor onto the
	 * designated output signal. */
	void
	processTargetReg(void)
	{
		AbstractRegister *reg;

		reg = desc.getTargetReg();
		out_dst_reg.write(*reg);
		delete reg;
	}

	/** Main thread */
	void
	thread_lt(void)
	{
		unsigned int i;
		sc_uint<22> ph_inc;
		sc_uint<22> addr_inc;
		burst_request<BUS_WIDTH,THREADS> req;
		unsigned int words;

		bool overflew;
		stride_descriptor d;
		sc_uint<32> addr;
		idx_t<THREADS> idx;

		while (true) {
			out_done.write(0);
			words = 0;

			switch (state) {
			case CMDGEN_ST_IDLE:
				if (!in_trigger.num_available() || in_ref_pending.read())
					break;

				state = CMDGEN_ST_FETCH;
				out_done.write(0);
				/* fall-through */
			case CMDGEN_ST_FETCH:
				if (in_trigger.num_available())
					in_trigger.read();

				if (!in_desc_fifo.num_available()) {
					if (out_req_fifo.num_free() == MC_BURSTREQ_FIFO_DEPTH) {
						state = CMDGEN_ST_IDLE;
						out_done.write(1);

						if (in_sched_opts.read()[WSS_STOP_DRAM_FINI]) {
							sc_stop();
						}
					}

					break;
				}

				/* Blocking */
				in_desc_fifo.read(desc);
				state = CMDGEN_ST_INIT_STATE;
				/* fall-through */
			case CMDGEN_ST_INIT_STATE:
				/* For WSS_NO_PARALLEL_DRAM_SP, consider DRAM
				 * and the SPs all together as a resource
				 * protected by a ticket lock. Only start
				 * processing request once my ticket is raised.
				 */
				if (in_sched_opts.read()[WSS_NO_PARALLEL_DRAM_SP] &&
				    in_ticket_pop.read() != desc.ticket)
					break;

				d = desc;
				out_dst.write(desc.dst);
				cycle_start = in_cycle.read();

				req.sp_offset = 0;

				if (desc.type == stride_descriptor::STRIDE) {
					state = CMDGEN_ST_RUNNING_STRIDE;
					req.pre_pol = PRECHARGE_LINEAR;

					if (desc.getTargetType() != TARGET_SP) {
						processTargetReg();
					}
					init_request_regs();
				} else {
					state = CMDGEN_ST_RUNNING_IDXIT;
					req.pre_pol = PRECHARGE_ALAP;

					if (desc.getTargetType() != TARGET_REG)
						throw invalid_argument("StrideSequencer: "
							"unsupported idxiterator request "
							"target");

					processTargetReg();
					out_idx_push_trigger.write(true);

					/* Blocking */
					in_idx.read(idx);
					addr = desc.addr + (idx.dram_off << 2);
				}
				break;
			case CMDGEN_ST_RUNNING_STRIDE:
				for (i = 0; i < BUS_WIDTH; i++) {
					req.wordmask[i] = word_mask_select(i);
					if (req.wordmask[i]) {
						words++;
						req.reg_offset[i] =
							regIdx(desc.getTargetType(), i);
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

				req.addr = global_addr;
				req.write = desc.write;
				req.target = desc.dst;
				if (req.target.type == TARGET_SP) {
					req.sp_offset = desc.dst_offset;
					desc.dst_offset += (words << 2);
					/* Overflew contains value of last
					 * iteration in the previous for-loop.*/
					if ((overflew || ph_inc == 0) &&
							line[BUS_WIDTH - 1] > 0)
						desc.dst_offset +=
							sp_line_addr_increment;
				}

				global_addr += (addr_inc << 2);
				local_idx += addr_inc;

				if (global_addr >= end_addr) {
					req.addr_next = 0xffffffff;
					req.last = true;
					out_req_fifo.write(req);
					state = CMDGEN_ST_WAIT_ALLPRE;
				} else {
					req.addr_next = global_addr;
					req.last = false;
					out_req_fifo.write(req);
				}
				break;
			case CMDGEN_ST_RUNNING_IDXIT:
				out_idx_push_trigger.write(false);
				/* Index iteration loads to vc.mem_data. */
				for (i = 0; i < BUS_WIDTH; i++) {
					if (i == (addr & ((BUS_WIDTH << 2)-1))>>2) {
						req.wordmask[i] = Log_1;
						req.reg_offset[i] = reg_offset_t<THREADS>(idx.cam_idx,0);
					} else {
						req.wordmask[i] = Log_0;
						req.reg_offset[i] = reg_offset_t<THREADS>();
					}
				}

				req.addr = addr & (~((BUS_WIDTH << 2)-1));
				req.write = desc.write;
				req.target = desc.dst;

				if (in_idx.num_available()){
					in_idx.read(idx);
					if (idx.dummy_last) {
						req.addr_next = 0xffffffff;
						req.last = true;
						out_req_fifo.write(req);
						state = CMDGEN_ST_WAIT_ALLPRE;
					} else {
						addr = desc.addr + (idx.dram_off << 2);
						req.addr_next = addr & (~((BUS_WIDTH << 2)-1));
						req.last = false;
						out_req_fifo.write(req);
					}
				}
				/* Else we don't commit yet, re-process same
				 * data */
				break;
			case CMDGEN_ST_WAIT_ALLPRE:
				if (in_DQ_allpre.read()) {
					state = CMDGEN_ST_FETCH;
					cycle_end = in_cycle.read();
					out_dst.write(RequestTarget());
					out_dst_reg.write(AbstractRegister());
					debug_print_fe(d, cycle_end - cycle_start);
				}
				break;
			}

			wait();
		}
	}
};

}

#endif /* MC_CONTROL_STRIDESEQUENCER_H */
