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

#include <systemc>

#include "sp/control/Scratchpad.h"
#include "util/SimdTest.h"

using namespace simd_test;
using namespace simd_model;
using namespace sc_core;
using namespace sc_dt;

namespace sp_test {

/** Test class for scratchpad. Mainly tests wiring, useful to warn about
 * wiring problems between the Scratchpad submodules.
 */
template <unsigned int BUS_WIDTH, unsigned int SIZE, unsigned int THREADS>
class Test_Scratchpad : public SimdTest
{
public:
	/** Scratchpad works on DRAM SDR clock. */
	sc_in<bool> in_clk{"in_clk"};

	/** Data written to DRAM/RF. */
	sc_in<sc_uint<32> > in_data[BUS_WIDTH];

	/** Scheduling options. */
	sc_inout<sc_bv<WSS_SENTINEL> > out_sched_opts{"out_sched_opts"};

	/** Ticket number that's ready to pop. */
	sc_inout<sc_uint<4> > out_ticket_pop{"out_ticket_pop"};

	/**************** RF interface ***************************/
	/** Stride descriptor FIFO. */
	sc_fifo_out<stride_descriptor> out_desc_fifo{"out_desc_fifo"};

	/** True iff execution should begin. */
	sc_fifo_out<bool> out_trigger{"out_trigger"};

	/** True iff transfer is completed. */
	sc_fifo_in<sc_uint<1> > in_wg_done{"in_wg_done"};

	/** R/W enable bit. */
	sc_in<bool> in_rf_enable{"in_rf_enable"};

	/** Write-bit (SP -> RF). When false, read from RF. */
	sc_in<bool> in_rf_write{"in_rf_write"};

	/** Register addressed by the SP, if any. */
	sc_in<AbstractRegister> in_rf_reg{"in_rf_reg"};

	/** Write-mask. */
	sc_in<sc_bv<BUS_WIDTH> > in_rf_mask{"in_rf_mask"};

	/** Data in from RF. */
	sc_inout<sc_uint<32> > out_rf_data[BUS_WIDTH];

	/** List of indexes read/written from RF, corresponding to each
	 * in_rf_data/out_rf_data word. */
	sc_in<reg_offset_t<THREADS> > in_rf_idx[BUS_WIDTH];

	/** Writeback mask from RF */
	sc_in<sc_bv<BUS_WIDTH> > out_rf_mask{"out_rf_mask"};

	/**************** DRAM interface *************************/
	/** Enable signal, when 1 perform read or write */
	sc_inout<bool> out_dram_enable{"out_dram_enable"};

	/** DRAM request destination. */
	sc_inout<RequestTarget> out_dram_dst{"out_dram_dst"};

	/** True iff operation is a write op */
	sc_inout<bool> out_dram_write{"out_dram_write"};

	/** Address to manipulate */
	sc_inout<sc_uint<18> > out_dram_addr{"out_dram_addr"};

	/** Data bus for write data */
	sc_inout<sc_uint<32> > out_dram_data[BUS_WIDTH];

	/** Write mask, aligned to in_data. */
	sc_inout<sc_bv<BUS_WIDTH> > out_dram_mask{"out_dram_mask"};

	SC_CTOR(Test_Scratchpad)
	{
		SC_THREAD(thread);
		sensitive << in_clk.pos();
	}

private:
	void
	thread(void)
	{
		/** @todo For now this just tests elaboration. Could add one or
		 * two proper tests later. */
		wait();

		test_finish();
	}
};

}

using namespace sp_test;
using namespace sp_control;

int
main(int argc, char **argv)
{
	unsigned int i;
	sc_clock clk("clk", sc_time(10.L/16.L, SC_NS));
	sc_signal<sc_bv<WSS_SENTINEL> > sched_opts;
	sc_signal<sc_uint<4> > ticket_pop;
	sc_signal<sc_uint<32> > data[4];
	sc_fifo<stride_descriptor> desc_fifo(1);
	sc_fifo<bool> trigger(2);
	sc_fifo<sc_uint<1> > wg_done;
	sc_signal<bool> rf_enable;
	sc_signal<bool> rf_write;
	sc_signal<AbstractRegister> rf_reg;
	sc_signal<sc_bv<4> > rf_mask;
	sc_signal<sc_bv<4> > i_rf_mask;
	sc_signal<sc_uint<32> > rf_data[4];
	sc_signal<reg_offset_t<1024> > rf_idx_w[4];

	sc_signal<bool> dram_enable;
	sc_signal<RequestTarget> dram_dst;
	sc_signal<bool> dram_write;
	sc_signal<sc_uint<18> > dram_addr;
	sc_signal<sc_uint<32> > dram_data[4];
	sc_signal<sc_bv<4> > dram_mask;

	Scratchpad<0,4,4,16384,1024> sp("sp");
	Test_Scratchpad<4,16384,1024> sp_test("sp_test");

	sp.in_clk(clk);
	sp.in_sched_opts(sched_opts);
	sp.in_ticket_pop(ticket_pop);
	sp.in_desc_fifo(desc_fifo);
	sp.in_trigger(trigger);
	sp.out_wg_done(wg_done);
	sp.out_rf_enable(rf_enable);
	sp.out_rf_write(rf_write);
	sp.out_rf_reg(rf_reg);
	sp.out_rf_mask(rf_mask);
	sp.in_rf_mask(i_rf_mask);
	sp.in_dram_enable(dram_enable);
	sp.in_dram_dst(dram_dst);
	sp.in_dram_write(dram_write);
	sp.in_dram_addr(dram_addr);
	sp.in_dram_mask(dram_mask);

	sp_test.in_clk(clk);
	sp_test.out_sched_opts(sched_opts);
	sp_test.out_ticket_pop(ticket_pop);
	sp_test.out_desc_fifo(desc_fifo);
	sp_test.out_trigger(trigger);
	sp_test.in_wg_done(wg_done);
	sp_test.in_rf_enable(rf_enable);
	sp_test.in_rf_write(rf_write);
	sp_test.in_rf_reg(rf_reg);
	sp_test.in_rf_mask(rf_mask);
	sp_test.out_rf_mask(i_rf_mask);
	sp_test.out_dram_enable(dram_enable);
	sp_test.out_dram_dst(dram_dst);
	sp_test.out_dram_write(dram_write);
	sp_test.out_dram_addr(dram_addr);
	sp_test.out_dram_mask(dram_mask);

	for (i = 0; i < 4; i++) {
		sp.out_data[i](data[i]);
		sp.in_rf_data[i](rf_data[i]);
		sp.out_rf_idx[i](rf_idx_w[i]);
		sp.in_dram_data[i](dram_data[i]);

		sp_test.in_data[i](data[i]);
		sp_test.out_rf_data[i](rf_data[i]);
		sp_test.in_rf_idx[i](rf_idx_w[i]);
		sp_test.out_dram_data[i](dram_data[i]);
	}

	sp.elaborate();

	sc_start(20, SC_NS);

	assert(sp_test.has_finished());

	return 0;
}
