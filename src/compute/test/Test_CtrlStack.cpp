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

#include "compute/control/CtrlStack.h"

#include "util/SimdTest.h"
#include "util/defaults.h"

using namespace sc_core;
using namespace sc_dt;
using namespace compute_model;
using namespace compute_control;
using namespace simd_test;

namespace compute_test {

/** Control stack test data. */
static ctrlstack_entry<COMPUTE_THREADS,11> entries_ptrn[]{
		{0xffffffff, 0x10, VSP_CTRL_BREAK},
		{0x00102030, 0x20, VSP_CTRL_RUN},
		{0xdeadbeef, 0x3, VSP_CTRL_RET},
		{0xa5a5a5a5, 0x9, VSP_CTRL_RET},
		{0x0c0ffefe, 0xa, VSP_CTRL_RUN},
};


/** Unit test for compute_control::CtrlStack */
template <unsigned int THREADS, unsigned int LANES, unsigned int PC_WIDTH,
	  unsigned int ENTRIES>
class Test_CtrlStack : public SimdTest
{
public:
	/** DRAM clock, SDR */
	sc_in<bool> in_clk{"in_clk"};

	/** (Synchronous) reset. */
	sc_inout<bool> out_rst{"out_rst"};

	sc_inout<sc_uint<1> > out_wg{"out_wg"};

	/** Action to perform this cycle (push, pop). */
	sc_inout<ctrlstack_action> out_action;

	/** Input entry, used for push. */
	sc_inout<ctrlstack_entry<THREADS,PC_WIDTH> > out_entry;

	/** Output of top entry. */
	sc_in<ctrlstack_entry<THREADS,PC_WIDTH> > in_top;

	/** Number of entries on the stack, "stack pointer". */
	sc_in<sc_uint<const_log2(ENTRIES) + 1> > in_sp;

	/** True iff the stack is full. */
	sc_in<bool> in_full;

	/** Exception: popping from empty list or pushing to full list */
	sc_in<bool> in_ex_overflow;

	/** Construct test thread */
	SC_CTOR(Test_CtrlStack)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();
	}
private:
	/** Main thread */
	void
	thread_lt(void)
	{
		ctrlstack_entry<THREADS,PC_WIDTH> entry;
		int i;
		const unsigned int entries = sizeof(entries_ptrn)/sizeof(entries_ptrn[0]);

		out_action.write(CTRLSTACK_IDLE);
		out_wg.write(0);

		/* Test 1: stack empty on reset */
		out_rst.write(1);
		wait();
		out_rst.write(0);
		wait();
		assert(in_sp.read() == 0);
		assert(in_full.read() == 0);
		assert(in_ex_overflow.read() == 0);

		/* Test 2: pop off this empty stack */
		out_action.write(CTRLSTACK_POP);
		wait();
		out_action.write(CTRLSTACK_IDLE);
		/* XXX: Should this take two cycles? */
		wait();
		assert(in_ex_overflow.read() != 0);
		assert(in_sp.read() == 0);

		/* Test 3: Push and pop all entries of idx */
		for (i = 0; i < entries + 1; i++) {
			if (i < entries) {
				out_entry.write(entries_ptrn[i]);
				out_action.write(CTRLSTACK_PUSH);
			} else {
				out_action.write(CTRLSTACK_IDLE);
			}
			wait();
			if (i > 0) {
				assert(in_sp.read() == i);
				assert(in_top.read() == entries_ptrn[i-1]);
				assert(in_ex_overflow.read() == 0);
				assert(in_full.read() == 0);
			}
		}

		for (i = entries; i > 0; i--) {
			out_action.write(CTRLSTACK_POP);
			wait();
			if (i < entries) {
				assert(in_sp.read() == i);
				assert(in_top.read() == entries_ptrn[i-1]);
				assert(in_ex_overflow.read() == 0);
				assert(in_full.read() == 0);
			}
		}
		out_action.write(CTRLSTACK_IDLE);
		wait();
		assert(in_sp.read() == 0);

		/* Test 4: overflow */
		for (i = 0; i < 17; i++) {
			if (i < 17) {
				out_entry.write(entries_ptrn[0]);
				out_action.write(CTRLSTACK_PUSH);
			}
			wait();
			if (i > 0 && i < 17) {
				assert(in_sp.read() == i);
				assert(in_top.read() == entries_ptrn[0]);
				assert(in_ex_overflow.read() == 0);
				assert(in_full.read() == (i == 16));
			}
		}

		out_action.write(CTRLSTACK_IDLE);
		wait();
		assert(in_ex_overflow.read() != 0);
		wait();
		assert(in_ex_overflow.read() == 0);

		for (i = 16; i >= 0; i--) {
			if (i > 0) {
				out_action.write(CTRLSTACK_POP);
			} else {
				out_action.write(CTRLSTACK_IDLE);
			}
			wait();
			if (i > 0) {
				assert(in_sp.read() == i);
				assert(in_top.read() == entries_ptrn[0]);
				assert(in_ex_overflow.read() == 0);
				assert(in_full.read() == (i == 16));
			}
		}
		assert(in_sp.read() == 0);
	}
};

}

using namespace compute_control;
using namespace compute_test;

int
sc_main(int argc, char* argv[])
{
	sc_signal<bool> rst;
	sc_signal<sc_uint<1> > wg;
	sc_signal<ctrlstack_action> action;
	sc_signal<ctrlstack_entry<COMPUTE_THREADS,11> > entry;
	sc_signal<ctrlstack_entry<COMPUTE_THREADS,11> > top;
	sc_signal<sc_uint<5> > sp;
	sc_signal<bool> full;
	sc_signal<bool> ex_overflow;

	sc_clock clk("clk", sc_time(10./12., SC_NS));

	CtrlStack<COMPUTE_THREADS,COMPUTE_FPUS,11,16>
			my_ctrlstack("my_ctrlstack");
	my_ctrlstack.in_clk(clk);
	my_ctrlstack.in_rst(rst);
	my_ctrlstack.in_wg(wg);
	my_ctrlstack.in_action(action);
	my_ctrlstack.in_entry(entry);
	my_ctrlstack.out_top(top);
	my_ctrlstack.out_sp(sp);
	my_ctrlstack.out_full(full);
	my_ctrlstack.out_ex_overflow(ex_overflow);

	Test_CtrlStack<COMPUTE_THREADS,COMPUTE_FPUS,11,16>
			my_ctrlstack_test("my_ctrlstack_test");
	my_ctrlstack_test.in_clk(clk);
	my_ctrlstack_test.out_rst(rst);
	my_ctrlstack_test.out_wg(wg);
	my_ctrlstack_test.out_action(action);
	my_ctrlstack_test.out_entry(entry);
	my_ctrlstack_test.in_top(top);
	my_ctrlstack_test.in_sp(sp);
	my_ctrlstack_test.in_full(full);
	my_ctrlstack_test.in_ex_overflow(ex_overflow);

	sc_core::sc_start(700, sc_core::SC_NS);

	return 0;
}
