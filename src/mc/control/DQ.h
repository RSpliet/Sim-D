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

#ifndef MC_CONTROL_DQ_H
#define MC_CONTROL_DQ_H

#include <systemc>
#include <cstdint>

#include "model/Register.h"
#include "mc/model/DQ_reservation.h"
#include "mc/control/Storage.h"

using namespace sc_core;
using namespace sc_dt;
using namespace std;
using namespace mc_model;
using namespace simd_model;

namespace mc_control {

/**
 * Data path (DQ) scheduler.
 * - Schedules data transfers back and forth between DRAM and SP.
 * - Simulation of storage system.
 * - Data (un)alignment?
 */
template <unsigned int BUS_WIDTH, unsigned int DRAM_BANKS,
	unsigned int DRAM_COLS, unsigned int DRAM_ROWS, unsigned int THREADS>
class DQ : public sc_module
{
public:
	/** SDR DRAM clock */
	sc_in<bool> in_clk{"in_clk"};

	/** Cycle counter */
	sc_in<long> in_cycle{"in_cycle"};

	/** DQ reservations */
	sc_fifo_in<DQ_reservation<BUS_WIDTH,DRAM_BANKS,THREADS> >
					in_fifo_DQ_res{"in_fifo_DQ_res"};

	/************ Write path to Register file ****************/
	/** Index within register to read/write to. */
	sc_inout<reg_offset_t<THREADS> > out_vreg_idx_w[BUS_WIDTH/4];

	/************* Read/write path to Scratchpad ***************/
	/** Scratchpad address */
	sc_inout<sc_uint<18> > out_sp_addr{"out_sp_addr"};

	/******* Lines shared between data path to Reg and SP *******/
	/** Data path is active. */
	sc_inout<bool> out_enable{"out_enable"};

	/** Data to write back to register */
	sc_inout<sc_uint<32> > out_data[4];

	/** Data read from the register file. */
	sc_in<sc_uint<32> > in_data[IF_SENTINEL][BUS_WIDTH/4];

	/** Register read/write mask. */
	sc_inout<sc_bv<BUS_WIDTH/4> > out_mask_w{"out_mask_w"};

	/** Writeback mask. Takes into consideration thread status. */
	sc_in<sc_bv<BUS_WIDTH/4> > in_reg_mask_w{"in_reg_mask_w"};

	/** Register/SP write bit. 0: read from reg/SP (write to DRAM), 1 write
	 * from DRAM to reg/SP. */
	sc_inout<bool> out_write{"out_write"};

	/** Construct thread */
	SC_CTOR(DQ)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();
	}


	/** Initialise a word in the storage back-end for testing and debugging
	 * purposes ("upload data")
	 * @param bank Bank.
	 * @param row Row.
	 * @param col Col.
	 * @param dq_word Subword of the DQ bus.
	 * @param val Value to write.
	 * @param print True iff the store should be printed to stdout. */
	void
	debug_store_init(sc_uint<const_log2(DRAM_BANKS)> bank,
			sc_uint<20> row, sc_uint<20> col, sc_uint<10> dq_word,
			uint32_t val, bool print = false)
	{
		store.set_word(bank, row, col, dq_word, val);
		if (print)
			cout << "(" << bank << "," << row << "," << col << ","
					<< dq_word << ") " << val << endl;
	}

	/** Read a word back from storage for debugging/testing purposes
	 * (validation)
	 * @param bank Bank
	 * @param row Row
	 * @param col Col
	 * @param dq_word Subword of the DQ bus
	 * @return the value stored at given address */
	uint32_t
	debug_store_read(sc_uint<const_log2(DRAM_BANKS)> bank,
			sc_uint<20> row, sc_uint<20> col, sc_uint<10> dq_word)
	{
		return store.get_word(bank, row, col, dq_word);
	}

private:
	/** Write requests encounter a two-cycle delay on the SRAM. Store all
	 * information required for the DRAM write-back of data. */
	typedef struct {
		/** True iff this pipeline contains a valid writeback command */
		bool valid;
		/** Beat */
		unsigned int beat;
		/** Accompanying DQ_reservation for physical address */
		DQ_reservation<BUS_WIDTH,DRAM_BANKS,THREADS> res;
		/** Scratchpad address for data phase. Could do with storing
		 * only phase information, but for simulation that'll make no
		 * difference */
		sc_uint<18> sp_addr;
		/** Normalised wordmask */
		unsigned int wordmask;
	} dq_pipe;
	/** Two pipeline stages worth of DRAM write-back requests */
	dq_pipe pipeline[2];

	/** States for this state machine */
	enum {
		DQ_IDLE,
		DQ_WAIT,
		DQ_BUSY
	} state = DQ_IDLE;

	/** Beat counter for current DRAM transaction. Synchronised to the
	 * scratchpad request. */
	unsigned int beat = 0;

	/** Storage back-end */
	Storage<BUS_WIDTH, DRAM_BANKS, DRAM_COLS, DRAM_ROWS> store;

	/** Perform a read-operation from DRAM and write-back to scratchpad or
	 * the register file.
	 *
	 * Data is pre-rotated to meet SP alignment requirements.
	 * @param DQ_res Reservation on the DQ
	 * @param wordmask Wordmask to write
	 * @param sp_addr Scratchpad address to write words to */
	void
	do_read(DQ_reservation<BUS_WIDTH,DRAM_BANKS,THREADS> &DQ_res,
			unsigned int wordmask, unsigned int sp_addr)
	{
		sc_uint<32> data[BUS_WIDTH/4];
		unsigned int i, j;
		sc_bv<BUS_WIDTH/4> mask;

		for (i = 0; i < BUS_WIDTH/4; i++) {
			out_data[i].write(0);
			mask[i] = Log_0;
		}

		j = (sp_addr >> 2) & ((BUS_WIDTH/4)-1);
		/* This rotation is superfluous for register writes, but let's
		 * roll with it. */
		for (i = 0; i < BUS_WIDTH/4; i++) {
			if (!(wordmask & (1 << i)))
				continue;

			/* Shift this word into data
			 * HW will have a series of muxes to
			 * do this efficiently without mul */
			out_data[j].write(store.get_word(DQ_res.bank, DQ_res.row,
					DQ_res.col | ((i & 0x2) >> 1) |
					(beat << 1),
					i & 0x1));
			mask[j] = Log_1;

			out_vreg_idx_w[j].write(DQ_res.reg_offset[i + (beat * BUS_WIDTH/4)]);

			j = (j + 1) % (BUS_WIDTH/4);
		}

		out_sp_addr.write(sp_addr);
		out_write.write(true);
		out_enable.write(true);
		out_mask_w.write(mask);
	}

	/** Request write-back data from the scratchpad/regfile.
	 * @param DQ_res Reservation on the DQ.
	 * @param wordmask Wordmask to write.
	 * @param sp_addr Address in the scratchpad. */
	void
	do_write_req_sp(DQ_reservation<BUS_WIDTH,DRAM_BANKS,THREADS> &DQ_res,
			unsigned int wordmask, unsigned int sp_addr)
	{
		unsigned int i;
		sc_bv<BUS_WIDTH/4> mask;

		for (i = 0; i < BUS_WIDTH/4; i++) {
			mask[i] = Log_0;
		}

		out_sp_addr.write(sp_addr);
		for (i = 0; i < BUS_WIDTH/4; i++) {
			if (!(wordmask & (1 << i)))
				continue;

			out_vreg_idx_w[i].write(DQ_res.reg_offset[i + (beat * BUS_WIDTH/4)]);
			mask[i] = Log_1;
		}

		out_write.write(false);
		out_enable.write(true);
		out_mask_w.write(mask);
	}

	/** Perform a write to the DRAM storage back-end.
	 * @param pipe Pipeline register struct for DRAM write-back metadata. */
	void
	do_write_storage(dq_pipe pipe)
	{
		unsigned int i;
		unsigned int j = 0;
		unsigned int lane;
		unsigned int intf;
		unsigned int data;
		sc_bv<BUS_WIDTH/4> wm = 0;

		wm.b_not();

		if (pipe.res.target.type == TARGET_SP)
			/* sp_addr of previous cycle. */
			j = (pipe.sp_addr >> 2) & ((BUS_WIDTH/4)-1);
		else
			wm = in_reg_mask_w.read();

		intf = int(pipe.res.target.get_interface());

		for (i = 0; i < BUS_WIDTH/4; i++) {
			if (!(pipe.wordmask & (1 << i)))
				continue;

			if (pipe.res.target.type == TARGET_SP) {
				lane = j;
				j = (j + 1) % (BUS_WIDTH/4);
			} else {
				lane = i;
			}

			data = in_data[intf][lane].read();

			if (wm[i])
				store.set_word(pipe.res.bank, pipe.res.row,
					pipe.res.col | ((i & 0x2) >> 1) |
					(pipe.beat << 1), i & 0x1, data);
		}
	}

	/** Main thread */
	void
	thread_lt(void)
	{
		unsigned int sp_addr;
		DQ_reservation<BUS_WIDTH,DRAM_BANKS,THREADS> DQ_res;

		unsigned int wordmask, words;

		while (true) {
			out_enable.write(false);

			if (pipeline[1].valid) {
				do_write_storage(pipeline[1]);
			}

			pipeline[1] = pipeline[0];
			pipeline[0].valid = 0;

			switch (state) {
			case DQ_IDLE:
				out_write.write(0);
				beat = 0;
				if (!in_fifo_DQ_res.num_available())
					break;

				in_fifo_DQ_res.read(DQ_res);

				assert (DQ_res.target.type == TARGET_SP ||
					DQ_res.sp_offset == 0);

				sp_addr = DQ_res.sp_offset;
				state = DQ_WAIT;

				/* fall-through */
			case DQ_WAIT:
				assert(DQ_res.cycle >= in_cycle.read());
				if (DQ_res.cycle != in_cycle.read())
					break;

				state = DQ_BUSY;
				/* fall-through */
			case DQ_BUSY:
				wordmask = DQ_res.wordmask.to_uint();
				wordmask >>= (beat * (BUS_WIDTH/4));
				wordmask &= (BUS_WIDTH - 1);
				words = __builtin_popcount(wordmask);

				if (wordmask) {
					if (DQ_res.write) {
						/* This data will return in
						 * two cycles */
						do_write_req_sp(DQ_res,wordmask,
								sp_addr);
						pipeline[0].valid = 1;
						pipeline[0].beat = beat;
						pipeline[0].res = DQ_res;
						pipeline[0].sp_addr = sp_addr;
						pipeline[0].wordmask = wordmask;
					} else {
						do_read(DQ_res, wordmask,
								sp_addr);
					}
				}

				if (beat == 3)
					state = DQ_IDLE;

				if (DQ_res.target.type == TARGET_SP)
					sp_addr += (words << 2);

				beat++;

				break;
			}

			wait();
		}
	}
};

}

#endif /* MC_CONTROL_DQ_H_*/
