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

#ifndef COMPUTE_CONTROL_IMEM_H_
#define COMPUTE_CONTROL_IMEM_H_

#include <vector>
#include <systemc>

#include "util/constmath.h"
#include "isa/model/Instruction.h"
#include "compute/model/imem_request.h"

using namespace std;
using namespace sc_core;
using namespace sc_dt;
using namespace compute_model;
using namespace isa_model;

namespace compute_control {

/**
 * Instruction memory, Harvard style.
 * @param BUS_WIDTH Number of 32-bit words in a burst.
 */
template <unsigned int PC_WIDTH = 11>
class IMem : public sc_core::sc_module
{
private:
	/** Storage for instruction memory */
	Instruction *imem;

public:
	/** Compute clock. */
	sc_in<bool> in_clk{"in_clk"};

	/** Program counter of current instruction */
	sc_fifo_in<imem_request<PC_WIDTH> > in_insn_r{"in_op_r"};

	/** Operation stored at pc */
	sc_inout<Instruction> out_op{"out_op"};

	/** PC of instruction at out_op */
	sc_inout<sc_uint<PC_WIDTH> > out_pc{"out_pc"};

	/** Operand to write.
	 * @todo Determine the width of this based on the width of the DRAM
	 * bus and the width of a single opcode. */
	sc_in<Instruction> in_op_w[4];

	/** PC to write */
	sc_in<sc_uint<PC_WIDTH> > in_pc_w{"in_pc_w"};

	/** Write? */
	sc_in<bool> in_w{"in_w"};

	/** Construct thread. */
	SC_CTOR(IMem)
	{
		imem = new Instruction[1 << PC_WIDTH];

		SC_THREAD(thread_rd);
		sensitive << in_clk.pos();

		SC_THREAD(thread_wr);
		sensitive << in_clk.pos();
	}

	/** Destructor. */
	~IMem(void) {
		delete[] imem;
	}

	/** Store an instruction into IMem. Used for debugging purposes and
	 * bring-up.
	 * @param i Index (PC) of instruction
	 * @param insn Instruction to store
	 *  */
	void
	debug_insn_store(unsigned int i, Instruction insn)
	{
		if (i >= (1 << PC_WIDTH))
			return;
		imem[i] = insn;

	}

private:
	/** Read port thread. */
	void
	thread_rd(void)
	{
		imem_request<PC_WIDTH> req;

		while (true) {
			wait();
			req = in_insn_r.read();

			/* Write back in next delta cycle */
			wait(SC_ZERO_TIME);

			if (req.valid) {
				out_pc.write(req.pc);
				out_op.write(imem[req.pc]);
			} else {
				out_pc.write(0);
				out_op.write(Instruction(NOP));
			}
		}
	}

	/** Write port thread. */
	void
	thread_wr(void)
	{
		while (true) {
			wait();

			if (in_w.read()) {
				imem[in_pc_w.read()] = in_op_w[0].read();
				imem[in_pc_w.read() + 1] = in_op_w[1].read();
				imem[in_pc_w.read() + 2] = in_op_w[2].read();
				imem[in_pc_w.read() + 3] = in_op_w[3].read();
			}
		}
	}
};

}

#endif /* COMPUTE_CONTROL_IMEM_H_ */
