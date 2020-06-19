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

#ifndef COMPUTE_CONTROL_CTRLSTACK_H
#define COMPUTE_CONTROL_CTRLSTACK_H

#include <cstdint>
#include <array>
#include <systemc>

#include "util/constmath.h"
#include "util/debug_output.h"
#include "compute/model/ctrlstack_entry.h"

using namespace sc_core;
using namespace sc_dt;
using namespace compute_model;
using namespace std;

namespace compute_control {

/**
 * Control stack.
 * @param THREADS Number of threads in a warp.
 * @param LANES number of execution lanes (subwarp length).
 * @param PC_WIDTH Width of the program counter in bits.
 * @param ENTRIES Max. number of stack entries.
 */
template <unsigned int THREADS, unsigned int LANES, unsigned int PC_WIDTH,
	  unsigned int ENTRIES>
class CtrlStack : public sc_module
{
private:
	/** Storage for stack entries */
	ctrlstack_entry<THREADS,PC_WIDTH> stack[2][ENTRIES];

	/** Stack pointer */
	sc_uint<const_log2(ENTRIES) + 1> sp[2];

public:
	/** Compute clock. */
	sc_in<bool> in_clk{"in_clk"};

	/** (Synchronous) reset. */
	sc_in<bool> in_rst{"in_rst"};

	/** Work-group slot for the current action. */
	sc_in<sc_uint<1> > in_wg{"in_wg"};

	/** Action to perform this cycle (push, pop). */
	sc_in<ctrlstack_action> in_action{"in_action"};

	/** Input entry, used for push. */
	sc_in<ctrlstack_entry<THREADS,PC_WIDTH> > in_entry{"in_entry"};

	/** Output of top entry. */
	sc_inout<ctrlstack_entry<THREADS,PC_WIDTH> > out_top{"out_top"};

	/** Number of entries on the stack, "stack pointer". */
	sc_inout<sc_uint<const_log2(ENTRIES) + 1> > out_sp{"out_sp"};

	/** True iff the stack is full. */
	sc_inout<bool> out_full{"out_full"};

	/** Exception: popping from empty list or pushing to full list */
	sc_inout<bool> out_ex_overflow{"out_ex_overflow"};

	/** Construct thread. */
	SC_CTOR(CtrlStack)
	{
		sp[0] = 0;
		sp[1] = 0;

		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();
	}

	/** Preload the stack for debugging purposes.
	 * @param e Control stack entry to push.
	 * @param wg Work-group slot number. */
	void
	debug_push(ctrlstack_entry<THREADS,PC_WIDTH> e, sc_uint<1> wg)
	{
		stack[wg][sp++] = e;
	}

private:
	/** Perform a reset of the control stack. */
	void
	do_rst(void)
	{
		out_sp.write(0);
		out_full.write(0);
		out_top.write(ctrlstack_entry<THREADS,PC_WIDTH>());
		out_ex_overflow.write(0);
	}

	/** Main thread */
	void
	thread_lt(void)
	{
		sc_uint<1> wg;

		do_rst();

		while (true) {
			if (in_rst.read()) {
				do_rst();
			} else {
				wg = in_wg.read();
				assert(sp[wg] <= ENTRIES);

				switch (in_action.read()) {
				case CTRLSTACK_PUSH:
					if (sp[wg] == ENTRIES) {
						out_ex_overflow.write(1);
					} else {
						out_ex_overflow.write(0);
						stack[wg][sp[wg]] = in_entry.read();

						sp[wg]++;
						out_sp.write(sp[wg]);
					}
					break;
				case CTRLSTACK_POP:
					if (sp[wg] == 0) {
						out_ex_overflow.write(1);
					} else {
						out_ex_overflow.write(0);
						sp[wg]--;
						out_sp.write(sp[wg]);
					}
					break;
				case CTRLSTACK_IDLE:
				default:
					out_ex_overflow.write(0);
					break;
				}

				out_full.write((sp[wg] == ENTRIES ? 1 : 0));

				if (debug_output[DEBUG_COMPUTE_TRACE])
					cout << sc_time_stamp() <<
						" CtrlStack: SP=" << sp[wg] << " ";

				if (sp[wg] > 0) {
					out_top.write(stack[wg][sp[wg]-1]);
					if (debug_output[DEBUG_COMPUTE_TRACE])
						cout << stack[wg][sp[wg]-1] << endl;
				} else {
					out_top.write(ctrlstack_entry<THREADS,PC_WIDTH>());
					if (debug_output[DEBUG_COMPUTE_TRACE])
						cout << ctrlstack_entry<THREADS,PC_WIDTH>() << endl;
				}
			}

			wait();
		}
	}
};

}

#endif /* COMPUTE_CONTROL_CTRLSTACK_H */
