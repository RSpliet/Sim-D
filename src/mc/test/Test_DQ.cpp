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

#include <cstdint>
#include <array>
#include <systemc>
#include <cassert>

#include "util/SimdTest.h"
#include "util/constmath.h"
#include "util/defaults.h"

#include "model/Register.h"

#include "mc/control/DQ.h"

using namespace sc_core;
using namespace sc_dt;
using namespace mc_model;
using namespace mc_control;
using namespace simd_test;
using namespace simd_model;

namespace mc_test {

const static uint32_t retval[][4]{
		{0x0,0x0,0x0,0x0},
		{0x0,0x0,0x0,0x0},
		{0x0,0x0,0x0,0x0},
		{0x0,0x0,0x0,0x0},
		{0x0,0x0,0x0,0x0},
		{0x0,0x0,0x0,0x0},
		{0xdeadbeef,0xbefdebab,0x0,0x0},
		{0x0,0x0,0xbadb105,0xaaaaaaaa},
		{0x0,0x0,0x0,0x0},
		{0x0,0x0,0x0,0x0},
		{0x1,0x2,0x3,0x0},
		{0x6,0x0,0x0,0x5},
		{0x0,0x9,0xa,0x0},
		{0x0,0x0,0x0,0xd},
};

/** Unit test for mc_control::DQ */
template <unsigned int BUS_WIDTH, unsigned int DRAM_BANKS,
	unsigned int DRAM_COLS, unsigned int DRAM_ROWS, unsigned int THREADS>
class Test_DQ : public SimdTest
{
public:
	/** Clock */
	sc_in<bool> in_clk{"in_clk"};

	/** Cycle counter */
	sc_inout<long> out_cycle{"out_cycle"};

	/** DQ reservation FIFO */
	sc_fifo_out<DQ_reservation<BUS_WIDTH,DRAM_BANKS,THREADS> >
				out_fifo_DQ_res{"out_fifo_dq_res"};

	/************ Write path to Register file ****************/
	/** Index within register to read/write to. */
	sc_in<reg_offset_t<THREADS> > in_vreg_idx_w[BUS_WIDTH/4];

	/************* Read/write path to Scratchpad ***************/
	/** Scratchpad address */
	sc_in<sc_uint<18> > in_sp_addr{"in_sp_addr"};

	/******* Lines shared between data path to Reg and SP *******/
	/** Data path is active. */
	sc_in<bool> in_enable{"in_enable"};

	/** Data to write back to register */
	sc_in<sc_uint<32> > in_data[4];

	/** Read data path */
	sc_inout<sc_uint<32> > out_data[IF_SENTINEL][BUS_WIDTH/4];

	/** Register read/write mask. */
	sc_in<sc_bv<BUS_WIDTH/4> > in_mask_w{"in_mask_w"};

	/** Writeback mask. Takes into consideration thread status. */
	sc_inout<sc_bv<BUS_WIDTH/4> > out_reg_mask_w{"out_reg_mask_w"};

	/** Write bit. 0: read */
	sc_in<bool> in_write{"in_write"};

	/** Construct test thread */
	SC_CTOR(Test_DQ)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();

		SC_THREAD(thread_cycle);
		sensitive << in_clk.pos();
	}

private:
	/** Main thread */
	void
	thread_lt(void)
	{
		unsigned int i, j;
		DQ_reservation<BUS_WIDTH,DRAM_BANKS,THREADS> res;

		res.target = RequestTarget(0,TARGET_SP);
		res.row = 12;
		res.col = 24;
		res.bank = 0;
		res.cycle = 5;
		res.sp_offset = 0x4000;
		res.wordmask = 0x0f5a;
		res.write = false;

		out_fifo_DQ_res.write(res);

		res.target = RequestTarget(0,TARGET_SP);
		res.row = 10;
		res.col = 8;
		res.bank = 12;
		res.cycle = 9;
		res.sp_offset = 0x2000;
		res.wordmask = 0x1337;
		res.write = false;
		out_fifo_DQ_res.write(res);

		wait();

		for (i = 0; i < sizeof(retval)/sizeof(retval[0]);
					i++) {
			for (j = 0; j < 3; j++)
				assert(retval[i][j] == in_data[j].read());

			std::cout << "@" << out_cycle.read() << ": " << std::hex <<
					in_sp_addr << ": " << in_data[0] << " "
					<< in_data[1] << " " << in_data[2] <<
					" " <<in_data[3] << " "	<< std::dec <<
					std::endl;
			wait();
		}

		test_finish();
	}

	void
	thread_cycle(void)
	{
		out_cycle.write(0);
		while (true) {
			wait();
			out_cycle.write(out_cycle.read() + 1);
		}
	}
};

}

using namespace mc_control;
using namespace mc_test;

int
sc_main(int argc, char* argv[])
{
	unsigned int i, j;
	sc_clock clk("clk", sc_time(10.L/12.L, SC_NS));

	sc_signal<long> cycle;
	sc_fifo<DQ_reservation<16,MC_DRAM_BANKS,1024> > fifo_DQ_res(4);

	sc_signal<reg_offset_t<1024> > vreg_idx_w[4];
	sc_signal<sc_uint<18> > sp_addr;
	sc_signal<sc_uint<32> > rd_data[IF_SENTINEL][4];
	sc_signal<sc_uint<32> > data[4];
	sc_signal<bool> enable;
	sc_signal<bool> write;
	sc_signal<sc_bv<4> > mask_w;
	sc_signal<sc_bv<4> > reg_mask_w;

	DQ<16,MC_DRAM_BANKS,MC_DRAM_COLS,MC_DRAM_ROWS,1024> my_dq("my_dq");
	my_dq.in_clk(clk);
	my_dq.in_cycle(cycle);
	my_dq.in_fifo_DQ_res(fifo_DQ_res);
	my_dq.out_enable(enable);
	my_dq.out_sp_addr(sp_addr);
	my_dq.out_mask_w(mask_w);
	my_dq.in_reg_mask_w(reg_mask_w);
	my_dq.out_write(write);

	Test_DQ<16,MC_DRAM_BANKS,MC_DRAM_COLS,MC_DRAM_ROWS,1024>
					my_dq_test("my_dq_test");
	my_dq_test.in_clk(clk);
	my_dq_test.out_cycle(cycle);
	my_dq_test.out_fifo_DQ_res(fifo_DQ_res);
	my_dq_test.in_enable(enable);
	my_dq_test.in_sp_addr(sp_addr);
	my_dq_test.in_mask_w(mask_w);
	my_dq_test.out_reg_mask_w(reg_mask_w);
	my_dq_test.in_write(write);

	for (i = 0; i < 4; i++) {
		my_dq.out_data[i](data[i]);
		my_dq.out_vreg_idx_w[i](vreg_idx_w[i]);

		my_dq_test.in_data[i](data[i]);
		my_dq_test.in_vreg_idx_w[i](vreg_idx_w[i]);

		for (j = 0; j < IF_SENTINEL; j++) {
			my_dq_test.out_data[j][i](rd_data[j][i]);
			my_dq.in_data[j][i](rd_data[j][i]);
		}
	}

	/* Upload initial test data */
	my_dq.debug_store_init(0, 12, 24, 1, 0xdeadbeef);
	my_dq.debug_store_init(0, 12, 25, 0, 0xcafe900d);
	my_dq.debug_store_init(0, 12, 25, 1, 0xbefdebab);
	my_dq.debug_store_init(0, 12, 26, 0, 0x0badb105);
	my_dq.debug_store_init(0, 12, 26, 1, 0x55555555);
	my_dq.debug_store_init(0, 12, 27, 0, 0xaaaaaaaa);

	for (i = 0; i < 16; i++) {
		my_dq.debug_store_init(12, 10, 8 + (i >> 1), i & 1, i + 1);
	}

	sc_core::sc_start(20, sc_core::SC_NS);

	assert(my_dq_test.has_finished());

	return 0;
}
