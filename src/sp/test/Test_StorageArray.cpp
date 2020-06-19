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

#include "util/constmath.h"
#include "util/defaults.h"
#include "util/SimdTest.h"

#include "sp/control/StorageArray.h"

using namespace sc_core;
using namespace sc_dt;
using namespace sp_control;
using namespace simd_test;
using namespace sp_model;

namespace sp_test {

/** Unit test for mc_control::Scratchpad */
template <unsigned int WG, unsigned int BUS_WIDTH, unsigned int SIZE_BYTES>
class Test_StorageArray : public SimdTest
{
public:
	/** Clock */
	sc_in<bool> in_clk{"in_clk"};

	/** Data bus to read from scratchpad. */
	sc_in<sc_uint<32> > in_data[BUS_WIDTH];

	/**************** DQ interface **************************/
	/** Input signals from the DQ schedulers */
	sc_inout<dq_pipe_sa<BUS_WIDTH> > out_dq_cmd{"in_dq_cmd"};

	/** Data coming from the RF. */
	sc_inout<sc_uint<32> > out_rf_data[BUS_WIDTH];

	/** Mask returned by the RF upon write. Used to skip disabled threads */
	sc_inout<sc_bv<BUS_WIDTH> > out_rf_mask;

	/**************** DRAM interface *************************/
	/** Enable bit for read/write to scratchpad. */
	sc_inout<bool> out_dram_enable{"out_dram_enable"};

	/** Destination of current DRAM traffic, to filter out writes to this
	 * specific storage array. */
	sc_inout<RequestTarget> out_dram_dst{"out_dram_dst"};

	/** Write bit for write to scratchpad. */
	sc_inout<bool> out_dram_write{"out_dram_write"};

	/** Address into scratchpad. */
	sc_inout<sc_uint<18> > out_dram_addr{"out_dram_addr"};

	/** Data bus to write into scratchpad. */
	sc_inout<sc_uint<32> > out_dram_data[BUS_WIDTH];

	/** Number of words to write into scratchpad. */
	sc_inout<sc_bv<BUS_WIDTH> > out_dram_mask{"out_dram_mask"};

	/** Construct test thread */
	SC_CTOR(Test_StorageArray)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();
	}

private:
	/** Print out current read data */
	void
	print_in_data()
	{
		unsigned int i;
		for (i = 0; i < 4; i++) {
			std::cout << std::hex << in_data[i].read() << std::dec
					<< " ";
		}
		std::cout << std::endl;
	}

	void
	test_dram_upload(void)
	{
		sc_bv<BUS_WIDTH> mask;

		// Write "1 2 3 0" to 0x2008,0x200c,0x2010,0x2014
		out_dram_enable.write(true);
		out_dram_write.write(true);
		out_dram_addr.write(0x2008);
		out_dram_data[2].write(1);
		out_dram_data[3].write(2);
		out_dram_data[0].write(3);
		out_dram_data[1].write(4);
		mask[0] = Log_1;
		mask[1] = Log_0;
		mask[2] = Log_1;
		mask[3] = Log_1;
		out_dram_mask.write(mask);
		wait();

		out_dram_write.write(0);
	}

	void
	test_dram(void)
	{
		sc_bv<BUS_WIDTH> mask;

		out_dram_write.write(false);
		out_dram_addr.write(0x2000);
		wait();
		print_in_data();

		out_dram_addr.write(0x2010);
		wait();
		print_in_data();
		assert(in_data[0].read() == 0x0);
		assert(in_data[1].read() == 0x0);
		assert(in_data[2].read() == 0x1);
		assert(in_data[3].read() == 0x2);

		out_dram_enable.write(0);
		wait();
		print_in_data();
		assert(in_data[0].read() == 0x3);
		assert(in_data[1].read() == 0x0);
		assert(in_data[2].read() == 0x0);
		assert(in_data[3].read() == 0x0);

		out_dram_addr.write(0x3000);
		wait();
		print_in_data();
		assert(in_data[0].read() == 0x3);
		assert(in_data[1].read() == 0x0);
		assert(in_data[2].read() == 0x0);
		assert(in_data[3].read() == 0x0);

		wait();
		print_in_data();

		/* Change 200c into 4. */
		out_dram_enable.write(true);
		out_dram_write.write(true);
		out_dram_addr.write(0x2008);
		mask[0] = Log_1;
		mask[1] = Log_1;
		mask[2] = Log_1;
		mask[3] = Log_1;
		out_dram_mask.write(mask);
		wait();
		print_in_data();
		out_dram_write.write(false);

		wait();
		print_in_data();
		wait();
		assert(in_data[0].read() == 0x3);
		assert(in_data[1].read() == 0x4);
		assert(in_data[2].read() == 0x1);
		assert(in_data[3].read() == 0x2);
		out_dram_enable.write(false);
	}

	void
	test_rf(void)
	{
		sc_bv<BUS_WIDTH> mask;
		dq_pipe_sa<BUS_WIDTH> psa;

		psa.addr = 0x2000;
		psa.mask_w = 0xf;
		psa.rw = true;
		psa.write_w = false;
		out_dq_cmd.write(psa);
		wait();
		psa.addr = 0x2010;
		out_dq_cmd.write(psa);
		wait();
		psa.rw = false;
		out_dq_cmd.write(psa);

		print_in_data();

		assert(in_data[0].read() == 0x0);
		assert(in_data[1].read() == 0x0);
		assert(in_data[2].read() == 0x1);
		assert(in_data[3].read() == 0x2);

		wait();
		print_in_data();

		assert(in_data[0].read() == 0x3);
		assert(in_data[1].read() == 0x4);
		assert(in_data[2].read() == 0x0);
		assert(in_data[3].read() == 0x0);
		psa.rw = false;
	}

	/** Main thread */
	void
	thread_lt(void)
	{
		sc_bv<BUS_WIDTH> mask;

		out_dram_dst.write(RequestTarget(0, TARGET_SP));

		test_dram_upload();
		test_dram();
		test_rf();
	}
};

}

using namespace sp_control;
using namespace sp_test;

int
sc_main(int argc, char* argv[])
{
	unsigned int i;
	sc_clock clk("clk", sc_time(10.L/16.L, SC_NS));

	sc_signal<dq_pipe_sa<4> > dq_cmd;
	sc_signal<bool> enable;
	sc_signal<RequestTarget> dram_dst;
	sc_signal<bool> write;
	sc_signal<sc_uint<32> > data_dram_rx[4];
	sc_signal<sc_uint<32> > data_tx[4];
	sc_signal<sc_uint<32> > data_rf_rx[4];
	sc_signal<sc_bv<4> > rf_mask;
	sc_signal<sc_uint<18> > addr;
	sc_signal<sc_bv<4> > mask;

	StorageArray<0,4,4,131072> sa("my_sp");
	Test_StorageArray<0,4,131072> sa_test("my_sp_test");

	sa.in_clk(clk);
	sa.in_dq_cmd(dq_cmd);
	sa.in_rf_mask(rf_mask);
	sa.in_dram_enable(enable);
	sa.in_dram_dst(dram_dst);
	sa.in_dram_write(write);
	sa.in_dram_addr(addr);
	sa.in_dram_mask(mask);

	sa_test.in_clk(clk);
	sa_test.out_dq_cmd(dq_cmd);
	sa_test.out_rf_mask(rf_mask);
	sa_test.out_dram_enable(enable);
	sa_test.out_dram_dst(dram_dst);
	sa_test.out_dram_write(write);
	sa_test.out_dram_addr(addr);
	sa_test.out_dram_mask(mask);

	for (i = 0; i < 4; i++) {
		sa.out_data[i](data_tx[i]);
		sa_test.in_data[i](data_tx[i]);
		sa.in_dram_data[i](data_dram_rx[i]);
		sa_test.out_dram_data[i](data_dram_rx[i]);
		sa.in_rf_data[i](data_rf_rx[i]);
		sa_test.out_rf_data[i](data_rf_rx[i]);
	}

	sc_core::sc_start(10, sc_core::SC_NS);

	return 0;
}
