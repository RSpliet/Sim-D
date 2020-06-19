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

#ifndef SP_SCRATCHPAD_H
#define SP_SCRATCHPAD_H

#include <systemc>
#include "sp/control/StrideSequencer.h"
#include "sp/control/DQ.h"
#include "sp/control/StorageArray.h"
#include "util/defaults.h"
#include "util/sched_opts.h"

using namespace sc_dt;
using namespace sc_core;
using namespace std;

namespace sp_control {

/** Scratchpad contained interface module.
 *
 * Instantiates the sp_control::StrideSequencer, sp_control::DQ and
 * sp_control::StorageArray submodules, wires them up and presents a uniform
 * interface to the instantiator.
 * @param BUS_WIDTH number of (32-bit) word outputs per cycle.
 * @param SIZE_BYTES size of the scratchpad in bytes.
 * @param THREADS Number of threads in a work-group.
 */
template <unsigned int WG, unsigned int BUS_WIDTH_DRAM, unsigned int BUS_WIDTH,
	unsigned int SIZE_BYTES,unsigned int THREADS>
class Scratchpad : public sc_module
{
public:
	/** Scratchpad works on DRAM SDR clock. */
	sc_in<bool> in_clk{"in_clk"};

	/** Data written to DRAM/RF */
	sc_inout<sc_uint<32> > out_data[BUS_WIDTH];

	/** Scheduling options. */
	sc_in<sc_bv<WSS_SENTINEL> > in_sched_opts{"in_sched_opts"};

	/** Ticket number that's ready to pop. */
	sc_in<sc_uint<4> > in_ticket_pop{"in_ticket_pop"};

	/**************** RF interface ***************************/
	/** Stride descriptor FIFO. */
	sc_fifo_in<stride_descriptor> in_desc_fifo{"in_desc_fifo"};

	/** True iff execution should begin. */
	sc_fifo_in<bool> in_trigger{"in_trigger"};

	/** True iff transfer is completed. FIFO to easily handle crossing
	 * clock domains. */
	sc_fifo_out<sc_uint<1> > out_wg_done{"out_wg_done"};

	/** R/W enable bit. */
	sc_inout<bool> out_rf_enable{"out_rf_enable"};

	/** Write-bit (SP -> RF). When false, read from RF. */
	sc_inout<bool> out_rf_write{"out_rf_write"};

	/** Register addressed by the SP, if any. */
	sc_inout<AbstractRegister> out_rf_reg{"out_rf_reg"};

	/** Write-mask. */
	sc_inout<sc_bv<BUS_WIDTH> > out_rf_mask{"out_rf_mask"};

	/** List of indexes read/written from RF, corresponding to each
	 * in_rf_data/out_rf_data word. */
	sc_inout<reg_offset_t<THREADS> > out_rf_idx[BUS_WIDTH];

	/** Data in from RF. */
	sc_in<sc_uint<32> > in_rf_data[BUS_WIDTH];

	/** Writeback mask from RF */
	sc_in<sc_bv<BUS_WIDTH> > in_rf_mask{"in_rf_mask"};

	/**************** DRAM interface *************************/
	/** Enable signal, when 1 perform read or write. */
	sc_in<bool> in_dram_enable{"in_dram_enable"};

	/** DRAM request destination. */
	sc_in<RequestTarget> in_dram_dst{"in_dram_dst"};

	/** True iff operation is a write op */
	sc_in<bool> in_dram_write{"in_dram_write"};

	/** Address to manipulate */
	sc_in<sc_uint<18> > in_dram_addr{"in_dram_addr"};

	/** Data bus for write data */
	sc_in<sc_uint<32> > in_dram_data[BUS_WIDTH_DRAM];

	/** Write mask, aligned to in_data. */
	sc_in<sc_bv<BUS_WIDTH_DRAM> > in_dram_mask{"in_dram_mask"};

	/** Constructor. */
	SC_CTOR(Scratchpad)
	: stridesequencer("stridesequencer"), dq("dq"),
	  storagearray("storagearray"),
	  stridesequencer_dq_fifo(1),
	  elaborated(false)
	{
		SC_THREAD(thread);
		sensitive << in_clk.pos();
	}

	/** Elaborate design. */
	void
	elaborate(void)
	{
		unsigned int i;

		stridesequencer.in_clk(in_clk);
		stridesequencer.in_desc_fifo(in_desc_fifo);
		stridesequencer.in_trigger(in_trigger);
		stridesequencer.out_dq_fifo(stridesequencer_dq_fifo);
		stridesequencer.out_wg_done(out_wg_done);
		stridesequencer.in_dq_done(dq_done);
		stridesequencer.out_rf_reg_w(out_rf_reg);
		stridesequencer.out_write(out_rf_write);
		stridesequencer.in_sched_opts(in_sched_opts);
		stridesequencer.in_ticket_pop(in_ticket_pop);

		dq.in_clk(in_clk);
		dq.in_read(out_rf_write);
		dq.in_dq_fifo(stridesequencer_dq_fifo);
		dq.out_rf_mask_w(out_rf_mask);
		dq.out_rf_rw(out_rf_enable);
		dq.out_sa_cmd(dq_sa_cmd);
		dq.out_done(dq_done);
		for (i = 0; i < BUS_WIDTH; i++)
			dq.out_rf_idx_w[i](out_rf_idx[i]);

		storagearray.in_clk(in_clk);
		storagearray.in_dq_cmd(dq_sa_cmd);
		storagearray.in_dram_enable(in_dram_enable);
		storagearray.in_dram_dst(in_dram_dst);
		storagearray.in_dram_write(in_dram_write);
		storagearray.in_dram_addr(in_dram_addr);
		storagearray.in_dram_mask(in_dram_mask);
		storagearray.in_rf_mask(in_rf_mask);
		for (i = 0; i < BUS_WIDTH_DRAM; i++) {
			storagearray.in_dram_data[i](in_dram_data[i]);
		}

		for (i = 0; i < BUS_WIDTH; i++) {
			storagearray.out_data[i](out_data[i]);
			storagearray.in_rf_data[i](in_rf_data[i]);
		}

		elaborated = true;
	}

	/** Upload a test pattern to the scratchpad.
	 * For now it just counts upwards from 0.
	 * @param addr Start address.
	 * @param words Number of words to upload. */
	void
	debug_upload_test_pattern(sc_uint<18> addr, unsigned int words)
	{
		storagearray.debug_upload_test_pattern(addr, words);
	}

private:
	/** The stride descriptor front-end. */
	StrideSequencer<WG,BUS_WIDTH,THREADS> stridesequencer;

	/** An idle pipeline stage to wait for register file data. */
	DQ<BUS_WIDTH,THREADS> dq;

	/** The storage array. */
	StorageArray<WG,BUS_WIDTH_DRAM, BUS_WIDTH,SIZE_BYTES> storagearray;

	/** FIFO from stride sequencer to DQ scheduler */
	sc_fifo<sp_model::DQ_reservation<BUS_WIDTH,THREADS> >
				stridesequencer_dq_fifo;

	/** DQ out signals to the storagearray */
	sc_signal<dq_pipe_sa<BUS_WIDTH> > dq_sa_cmd;

	/** DQ signalling it's done with the active request. */
	sc_signal<bool> dq_done;

	/** True if elaboration finished. */
	bool elaborated;

	/** Default thread */
	void
	thread(void)
	{
		assert(elaborated);

		while(true) {
			wait();
		}
	}
};

}

#endif /* SP_SCRATCHPAD_H */
