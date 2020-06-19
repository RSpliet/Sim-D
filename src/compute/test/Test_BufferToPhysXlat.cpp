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

#include "compute/control/BufferToPhysXlat.h"

using namespace sc_core;
using namespace sc_dt;
using namespace simd_test;

namespace compute_test {

/* Keep these in order of monotonically increasing idx */
struct {
	unsigned int idx;
	sc_uint<32> phys;
} entries[] = {
		{0, 0x4000},
		{8, 0x14000},
		{31, 0x2854000}
};

/** Unit test for BufferToPhysXlat */
template <unsigned int ENTRIES>
class Test_BufferToPhysXlat : public SimdTest
{
public:
	/** Compute clock */
	sc_in<bool> in_clk{"in_clk"};

	/** Synchronous? reset signal */
	sc_inout<bool> out_rst{"out_rst"};

	/** Requested buffer ID */
	sc_in<Buffer> in_phys{"in_phys"};

	/** Physical address of id in in_id */
	sc_inout<sc_uint<const_log2(ENTRIES)> > out_idx{"out_idx"};

	/** Perform a write */
	sc_inout<bool> out_w{"out_w"};

	/** Index to write to */
	sc_inout<sc_uint<const_log2(ENTRIES)> > out_idx_w{"out_idx_w"};

	/** Physical address to set idx to */
	sc_inout<Buffer> out_phys_w{"out_phys_w"};

	/** Construct test thread */
	SC_CTOR(Test_BufferToPhysXlat)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();
	}

private:
	void
	test_rst(void)
	{
		unsigned int i;

		out_rst.write(true);
		wait();
		out_rst.write(false);
		wait();

		for (i = 0; i < ENTRIES; i++) {
			out_idx.write(i);
			wait();
			assert(in_phys.read().valid == false);
		}

	}

	void
	test_ud(void)
	{
		unsigned int e_count = sizeof(entries) / sizeof(entries[0]);
		unsigned int i, e;

		out_w.write(true);
		for (e = 0; e < e_count; e++) {
			out_idx_w.write(entries[e].idx);
			out_phys_w.write(Buffer(entries[e].phys));
			wait();
		}
		out_w.write(false);

		for (i = 0, e = 0; i < ENTRIES && e < e_count; i++) {
			out_idx.write(i);
			wait();

			if (entries[e].idx == i) {
				assert(in_phys.read().getAddress() == entries[e].phys);
				e++;
			} else {
				assert(in_phys.read().valid == false);
			}
		}
	}

	/** Main thread */
	void
	thread_lt(void)
	{
		test_rst();
		test_ud();

		test_finish();
	}
};

}

using namespace compute_test;
using namespace compute_control;

int
sc_main(int argc, char* argv[])
{

	sc_signal<bool> rst;
	sc_signal<Buffer> xlat_phys;
	sc_signal<sc_uint<const_log2(MC_BIND_BUFS)> > xlat_idx;
	sc_signal<bool> w;
	sc_signal<sc_uint<const_log2(MC_BIND_BUFS)> > idx_w;
	sc_signal<Buffer> phys_w;

	sc_clock clk("clk", sc_time(10./12., SC_NS));

	BufferToPhysXlat<MC_BIND_BUFS> my_xlat("my_xlat");
	my_xlat.in_clk(clk);
	my_xlat.in_rst(rst);
	my_xlat.in_idx(xlat_idx);
	my_xlat.out_phys(xlat_phys);
	my_xlat.in_w(w);
	my_xlat.in_idx_w(idx_w);
	my_xlat.in_phys_w(phys_w);

	/* Initialise default lookup values */
	//my_xlat.set(0, 0x4000);
	//my_xlat.set(8, 0x14000);
	//my_xlat.set(31, 0x2854000);

	Test_BufferToPhysXlat<MC_BIND_BUFS> my_xlat_test("my_xlat_test");
	my_xlat_test.in_clk(clk);
	my_xlat_test.out_rst(rst);
	my_xlat_test.in_phys(xlat_phys);
	my_xlat_test.out_idx(xlat_idx);
	my_xlat_test.out_w(w);
	my_xlat_test.out_idx_w(idx_w);
	my_xlat_test.out_phys_w(phys_w);

	sc_core::sc_start(100, sc_core::SC_NS);

	assert(my_xlat_test.has_finished());

	return 0;
}
