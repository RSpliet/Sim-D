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

#include "compute/control/IMem.h"
#include "util/SimdTest.h"

using namespace sc_core;
using namespace sc_dt;
using namespace compute_model;
using namespace compute_control;
using namespace isa_model;
using namespace simd_test;

namespace compute_test {

/* XXX: Write some macros for shorter notation... */

static Instruction op_ptrn[]{
		Instruction(OP_TEST,{.test = TEST_NZ},Operand(REGISTER_PR,0),Operand(REGISTER_VGPR,3)),
		Instruction(NOP,{}),
		Instruction(OP_CPUSH,{.cpush = CPUSH_BRK},{},Operand(3)),
		Instruction(OP_J,{},{},Operand(8)),
		Instruction(OP_BRK,{},{},Operand(REGISTER_PR,0)),
		Instruction(OP_J,{},{},Operand(4)),
		Instruction(OP_CPOP,{}),
};

/** Unit test for compute_control::IMem. */
template <unsigned int PC_WIDTH>
class Test_IMem : public SimdTest
{
public:
	/** DRAM clock, SDR */
	sc_in<bool> in_clk;

	/** (Synchronous) reset. */
	sc_fifo_out<imem_request<PC_WIDTH> > out_insn_r{"out_insn_r"};

	/** Action to perform this cycle (push, pop). */
	sc_in<Instruction> in_op{"in_op"};

	/** Relayed PC corresponding with the instruction */
	sc_in<sc_uint<PC_WIDTH> > in_pc{"in_pc"};

	/** Operand to write */
	sc_inout<Instruction> out_op_w[4];

	/** PC to write */
	sc_inout<sc_uint<PC_WIDTH> > out_pc_w{"out_pc_w"};

	/** Write? */
	sc_inout<bool> out_w{"out_w"};

	/** Construct test thread */
	SC_CTOR(Test_IMem)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();
	}
private:
	/** Main thread */
	void thread_lt(void)
	{
		Instruction entry;
		unsigned int i, j;
		const unsigned int entries = sizeof(op_ptrn)/sizeof(op_ptrn[0]);
		imem_request<PC_WIDTH> req;

		req.valid = true;

		wait();
		out_w.write(true);
		for (i = 0; i < entries; i += 4) {
			out_pc_w.write(i);

			for (j = 0; j < 4; j++) {
				if (i+j < entries)
					out_op_w[j].write(op_ptrn[i+j]);
				else
					out_op_w[j].write(Instruction());
			}
			wait();
		}
		out_w.write(false);

		wait();

		req.pc = 0;
		out_insn_r.write(req);
		wait();

		//wait();
		for (i = 0; i < entries; i++) {
			req.pc = i + 1;
			out_insn_r.write(req);
			wait(SC_ZERO_TIME); /* Give IMem a chance to run too */

			entry = in_op.read();
			cout << sc_time_stamp() << hex << " " << i << ": " << dec << entry << endl;

			assert(entry == op_ptrn[i]);

			wait();
		}
		wait(0, SC_NS);
		wait();

		req.valid = false;
		out_insn_r.write(req);
		wait();
		entry = in_op.read();
		cout << sc_time_stamp() << hex << " X: " << dec << entry << endl;

		assert(entry == Instruction(NOP));
	}
};

}

using namespace compute_control;
using namespace compute_test;

int
sc_main(int argc, char* argv[])
{
	sc_signal<Instruction> op;
	sc_fifo<imem_request<11> > insn_r(1);
	sc_signal<sc_uint<11> > pc_o;

	sc_signal<Instruction> op_w[4];
	sc_signal<sc_uint<11> > pc_w;
	sc_signal<bool> w;
	unsigned int i;

	sc_clock clk("clk", sc_time(10./12., SC_NS));

	IMem<11> my_imem("my_imem");
	my_imem.in_clk(clk);
	my_imem.in_insn_r(insn_r);
	my_imem.out_op(op);
	my_imem.out_pc(pc_o);
	my_imem.in_pc_w(pc_w);
	my_imem.in_w(w);

	Test_IMem<11> my_imem_test("my_imem_test");
	my_imem_test.in_clk(clk);
	my_imem_test.out_insn_r(insn_r);
	my_imem_test.in_op(op);
	my_imem_test.in_pc(pc_o);
	my_imem_test.out_pc_w(pc_w);
	my_imem_test.out_w(w);

	for (i = 0; i < 4; i++) {
		my_imem.in_op_w[i](op_w[i]);
		my_imem_test.out_op_w[i](op_w[i]);
	}

	sc_core::sc_start(700, sc_core::SC_NS);

	return 0;
}
