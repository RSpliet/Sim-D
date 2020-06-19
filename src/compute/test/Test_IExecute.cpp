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

#include "compute/control/IExecute.h"
#include "model/Buffer.h"
#include "util/SimdTest.h"
#include "util/constmath.h"
#include "util/defaults.h"

using namespace sc_core;
using namespace sc_dt;
using namespace compute_control;
using namespace compute_model;
using namespace simd_test;
using namespace simd_model;
using namespace std;

namespace compute_test {

/** Unit test for compute_control::IExecute
 * @param PC_WIDTH Width of the program counter in bits.
 * @param THREADS Number of threads in a warp.
 * @param LANES Number of parallel lanes in one SIMD cluster.
 * @param CSTACK_ENTRIES Max. number of entries in the control stack.
 * @todo Test in_dequeue_sb. */
template <unsigned int PC_WIDTH, unsigned int THREADS = 1024,
		unsigned int FPUS = 128, unsigned int RCPUS = 32,
		unsigned int CSTACK_ENTRIES = 16>
class Test_IExecute : public SimdTest
{
private:
	unsigned int pipe_depth;
public:
	/** Compute clock. */
	sc_in<bool> in_clk{"in_clk"};

	/** The PC associated with this instruction */
	sc_inout<sc_uint<PC_WIDTH> > out_pc{"out_pc"};

	/** Instruction fetched by IFetch. */
	sc_inout<Instruction> out_insn{"out_insn"};

	/** Workgroup associated with this instruction */
	sc_inout<sc_uint<1> > out_wg{"out_wg"};

	/** Identifier of currently active warp - important for write-back. */
	sc_inout<sc_uint<const_log2(THREADS/FPUS)> > out_col_w{"out_col_w"};

	/** Identifier of currently active warp - important for write-back. */
	sc_inout<sc_uint<const_log2(FPUS/RCPUS)> > out_subcol_w{"out_subcol_w"};

	/** Inputs for instruction. For scalar->scalar, values in lane 0. For
	 * vector + scalar -> vector/pred, scalar replicated in all lanes.*/
	sc_inout<sc_uint<32> > out_operand[3][FPUS];

	sc_inout<stride_descriptor> out_sd[2];

	/** At least one thread is active. */
	sc_inout<sc_bv<2> > out_thread_active{"out_thread_active"};

	/** Physical address associated with incoming ld/st instruction */
	sc_inout<Buffer> out_xlat_phys{"out_xlat_phys"};

	/** Physical address associated with incoming ld/st instruction */
	sc_in<Buffer> out_sp_xlat_phys{"out_xlat_sp_phys"};

	/***************** Signals for PC write-back ***********************/

	/** PC to write back in case of a branch operation */
	sc_in<sc_uint<PC_WIDTH> > in_pc_w{"in_pc_w"};

	/** True iff a branch operation leads to writing a different PC */
	sc_in<bool> in_pc_do_w{"in_pc_do_w"};

	/***************** Signals for register write-back *****************/

	/** Write request. */
	sc_in<Register<THREADS/FPUS> > in_req_w{"in_req_w"};

	/** Workgroup for which the write mask should be ready next cycle */
	sc_in<sc_uint<1> > in_wg_w{"in_wg_w"};

	/** Output of instruction. For scalar, result in lane 0. */
	sc_in<sc_uint<32> > in_data_w[FPUS];

	/** Results must be written back? */
	sc_in<bool> in_w{"in_w"};

	/** Scoreboard entry must be removed? */
	sc_in<bool> in_dequeue_sb{"in_dequeue_sb"};

	/** Scoreboard cstack entry must be removed? */
	sc_in<bool> in_dequeue_sb_cstack_write
				{"in_dequeue_sb_cstack_write"};

	/** Results must really really be written back? Used for CPOP. */
	sc_in<bool> in_ignore_mask_w{"in_ignore_mask_w"};

	/* Column to retreive write mask for writeback stage. */
	sc_fifo_in<sc_uint<const_log2(THREADS/FPUS)> >
				in_col_mask_w{"in_col_mask_w"};

	/***************** IO signals for control stack ********************/

	/** Action to perform on stack. */
	sc_in<ctrlstack_action> in_cstack_action{"in_cstack_action"};

	/** CStack entry to push on CPUSH. */
	sc_in<ctrlstack_entry<THREADS,PC_WIDTH> >
				in_cstack_entry{"in_cstack_entry"};

	/** Top of stack */
	sc_inout<ctrlstack_entry<THREADS,PC_WIDTH> >
				out_cstack_top{"out_cstack_top"};

	/** Stack pointer */
	sc_inout<sc_uint<const_log2(CSTACK_ENTRIES) + 1> >
				out_cstack_sp{"out_cstack_sp"};

	/** Is the cstack full? */
	sc_inout<bool> out_cstack_full{"out_cstack_full"};

	/** Are we doing something illegal while the CSTACK is full/empty? */
	sc_inout<bool> out_cstack_ex_overflow{"out_cstack_ex_overflow"};

	/*********************** Work parameters **************************/

	/** Workgroup offsets (X, Y) */
	sc_inout<sc_uint<32> > out_wg_off[2][2];

	/** Workgroup dimensions (X, Y) */
	sc_inout<sc_uint<32> > out_dim[2];

	/** Dimension of the workgroups */
	sc_inout<workgroup_width> out_wg_width{"out_wg_width"};

	/************************** DRAM Request **************************/
	/** DRAM request */
	sc_fifo_in<stride_descriptor> in_desc_fifo[3];

	/** Kick off request */
	sc_fifo_in<bool> in_store_kick[3];

	/** Per-WG block signal (on e.g. DRAM) */
	sc_in<workgroup_state> in_wg_state_next[2];

	/** Per-WG exit commit signal. */
	sc_in<sc_bv<2> > in_wg_exit_commit{"in_wg_exit_commit"};

	/** Construct test thread */
	SC_CTOR(Test_IExecute) : pipe_depth(3)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();
	}
private:
	void
	wait(void)
	{
		unsigned int i;

		for (i = 0; i < pipe_depth; i++) {
			sc_module::wait();
			out_insn.write(Instruction());
		}
	}

	void
	wait(const sc_time t)
	{
		sc_module::wait(t);
	}

	/** Test MAD, both with VGPR and SGPR */
	void
	test_MAD(void)
	{
		bfloat intm;
		bfloat res;
		Register<THREADS/FPUS> req;

		const Instruction op = Instruction(OP_MAD,{},
			Operand(REGISTER_VGPR, 0),Operand(REGISTER_VGPR, 0),
			Operand(REGISTER_VGPR, 1),Operand(REGISTER_VGPR, 2));

		const Instruction op2 = Instruction(OP_MAD,{},
			Operand(REGISTER_VGPR, 0),Operand(REGISTER_VGPR, 0),
			Operand(REGISTER_VGPR, 1),Operand(REGISTER_VGPR, 2));

		out_col_w.write(0);
		for (unsigned int i = 0; i < FPUS; i++) {
			intm.f = i;
			out_operand[0][i].write(intm.b);
			intm.f = ((float)FPUS) - intm.f;
			out_operand[1][i].write(intm.b);
			intm.f = 12.f;
			out_operand[2][i].write(intm.b);
		}
		out_insn.write(op);

		wait();
		assert(in_col_mask_w.read() == 0);
		req = in_req_w.read();

		for (unsigned int i = 0; i < FPUS; i++) {
			res.b = in_data_w[i].read();
			intm.f = i * ((float)FPUS-i) + 12.f;
			assert(intm.b == res.b);
		}
		assert(op.getDst() == req);
		assert(req.col == 0);

		intm.f = 3.f;
		out_col_w.write(1);
		for (unsigned int i = 0; i < FPUS; i++)
			out_operand[1][i].write(intm.b);
		out_insn.write(op2);

		wait();
		assert(in_col_mask_w.read() == 1);
		req = in_req_w.read();
		for (unsigned int i = 0; i < FPUS; i++) {
			res.b = in_data_w[i].read();
			intm.f = i * 3.f + 12.f;
			assert(intm.b == res.b);
		}
		assert(op2.getDst() == req);
		assert(req.col == 1);
	}

	/** Test MUL. */
	void
	test_MUL(void)
	{
		bfloat intm;
		bfloat res;
		Register<THREADS/FPUS> req;
		const Instruction op = Instruction(OP_MUL,{},
			Operand(REGISTER_VGPR, 3), Operand(REGISTER_VGPR, 0),
			Operand(REGISTER_VGPR, 1));

		out_insn.write(op);
		out_col_w.write(2);
		for (unsigned int i = 0; i < FPUS; i++) {
			intm.f = i;
			out_operand[0][i].write(intm.b);
			intm.f = ((float)FPUS) - intm.f;
			out_operand[1][i].write(intm.b);
		}

		wait();
		assert(in_col_mask_w.read() == 2);
		req = in_req_w.read();
		for (unsigned int i = 0; i < FPUS; i++) {
			res.b = in_data_w[i].read();
			intm.f = i * (((float)FPUS)-i);
			assert(intm.b == res.b);
		}
		assert(op.getDst() == req);
		assert(req.col == 2);
	}

	/** Test ADD. */
	void
	test_ADD(void)
	{
		bfloat intm;
		bfloat res;
		Register<THREADS/FPUS> req;
		const Instruction op = Instruction(OP_ADD,{},
			Operand(REGISTER_VGPR, 0), Operand(REGISTER_VGPR, 0),
			Operand(REGISTER_VGPR, 1));

		out_insn.write(op);
		out_col_w.write(2);
		for (unsigned int i = 0; i < FPUS; i++) {
			intm.f = i;
			out_operand[0][i].write(intm.b);
			intm.f = ((float)FPUS) - i;
			out_operand[1][i].write(intm.b);
		}

		wait();
		assert(in_col_mask_w.read() == 2);
		req = in_req_w.read();
		for (unsigned int i = 0; i < FPUS; i++) {
			res.b = in_data_w[i].read();
			intm.f = (float)FPUS;
			assert(intm.b == res.b);
		}
		assert(op.getDst() == req);
		assert(req.col == 2);
	}

	/** Test TEST operation, predicate tests. */
	void
	test_TEST(void)
	{
		bfloat intm;
		bfloat res;
		Register<THREADS/FPUS> req;
		const Instruction op = Instruction(OP_TEST,{.test = TEST_GE},
			Operand(REGISTER_PR, 1),Operand(REGISTER_VGPR, 0));

		out_insn.write(op);
		out_col_w.write(min((THREADS/FPUS) - 1, 4u));
		for (unsigned int i = 0; i < FPUS; i++) {
			intm.f = i;
			if ((i % 2) == 0)
				intm.f = -intm.f;
			out_operand[0][i].write(intm.b);
		}

		wait();
		assert(in_col_mask_w.read() == min((THREADS/FPUS) - 1, 4u));
		req = in_req_w.read();
		res.b = in_data_w[0].read();
		assert(res.b);

		for (unsigned int i = 1; i < FPUS; i++) {
			res.b = in_data_w[i].read();
			assert(res.b == (i % 2));
		}
		assert(op.getDst() == req);
		assert(req.col == min((THREADS/FPUS) - 1, 4u));
	}

	/** Test PBOOL predicate boolean and,or operations. */
	void
	test_PBOOL(void)
	{
		unsigned int res;
		Register<THREADS/FPUS> req;
		const Instruction op_or = Instruction(OP_PBOOL,{.pbool = PBOOL_OR},
			Operand(REGISTER_PR, 1),Operand(REGISTER_PR, 0),Operand(REGISTER_PR, 1));
		const Instruction op_and = Instruction(OP_PBOOL,{.pbool = PBOOL_AND},
			Operand(REGISTER_PR, 3),Operand(REGISTER_PR, 0),Operand(REGISTER_PR, 1));

		out_insn.write(op_or);
		out_col_w.write(0);
		for (unsigned int i = 0; i < FPUS; i++) {
			out_operand[0][i].write((i % 2));
			out_operand[1][i].write((i % 2) == 0);
		}

		wait();

		assert(in_col_mask_w.read() == 0);
		req = in_req_w.read();
		for (unsigned int i = 1; i < FPUS; i++) {
			res = in_data_w[i].read();
			assert(res == 1);
		}
		assert(op_or.getDst() == req);
		assert(req.col == 0);

		out_insn.write(op_and);
		out_col_w.write(2);
		for (unsigned int i = 0; i < FPUS; i++) {
			out_operand[0][i].write((i % 2));
			out_operand[1][i].write((i % 4) > 2);
		}

		wait();
		assert(in_col_mask_w.read() == 2);
		req = in_req_w.read();
		for (unsigned int i = 1; i < FPUS; i++) {
			res = in_data_w[i].read();
			assert(res == ((i % 4) == 3));
		}
		assert(op_and.getDst() == req);
		assert(req.col == 2);
	}

	/** CMASK instruction, used for continue, break, ret, exit etc. */
	void
	test_CMASK(void)
	{
		unsigned int i;
		Register<THREADS/FPUS> req;

		const Instruction op_exit = Instruction(OP_EXIT,{},
			Operand(REGISTER_VSP,VSP_CTRL_EXIT), Operand(REGISTER_PR, 2));
		const Instruction op_brk = Instruction(OP_BRK,{},
			Operand(REGISTER_VSP,VSP_CTRL_BREAK), Operand(REGISTER_PR, 1));
		const Instruction op_cmask = Instruction(OP_CMASK,{},
			Operand(REGISTER_VSP,VSP_CTRL_RUN), Operand(REGISTER_PR, 0));

		out_insn.write(op_exit);
		out_col_w.write(0);
		for (i = 0; i < FPUS; i++)
			out_operand[0][i].write((i % 2));

		wait();

		assert(in_col_mask_w.read() == 0);
		req = in_req_w.read();
		for (i = 0; i < FPUS; i++)
			assert(in_data_w[i].read() == (!(i % 2)));
		assert(Operand(REGISTER_VSP,VSP_CTRL_EXIT) == req);
		assert(req.col == 0);

		out_insn.write(op_brk);
		out_col_w.write(1);
		wait();

		assert(in_col_mask_w.read() == 1);
		req = in_req_w.read();
		for (i = 0; i < FPUS; i++)
			assert(in_data_w[i].read() == (!(i % 2)));
		assert(Operand(REGISTER_VSP,VSP_CTRL_BREAK) == req);
		assert(req.col == 1);

		out_insn.write(op_cmask);
		out_col_w.write(2);
		for (i = 0; i < FPUS; i++)
			out_operand[0][i].write((i < 64));
		wait();

		assert(in_col_mask_w.read() == 2);
		req = in_req_w.read();
		for (i = 0; i < FPUS; i++)
			assert(in_data_w[i].read() == (!(i < 64)));
		assert(Operand(REGISTER_VSP,VSP_CTRL_RUN) == req);
		assert(req.col == 2);
	}

	/** Test the MOV instruction, untyped reg->reg mov */
	void
	test_MOV(void)
	{
		Register<THREADS/FPUS> req;
		const Instruction op_mov = Instruction(OP_MOV,{},
			Operand(REGISTER_VGPR, 2), Operand(3));
		unsigned int i;

		out_insn.write(op_mov);
		out_col_w.write(3);
		for (i = 0; i < FPUS; i++)
			out_operand[0][i].write(3);

		wait();

		assert(in_col_mask_w.read() == 3);
		req = in_req_w.read();
		for (i = 0; i < FPUS; i++)
			assert(in_data_w[i].read() == 3);
		assert(op_mov.getDst() == req);
		assert(req.col == 3);
	}

	/** Test CVT, move register to register values converting between int
	 * and float. */
	void
	test_CVT(void)
	{
		Register<THREADS/FPUS> req;
		const Instruction op_cvt_i2f = Instruction(OP_CVT,{.cvt = CVT_I2F},
			Operand(REGISTER_VGPR, 2), Operand(REGISTER_VSP, VSP_TID_X));
		const Instruction op_cvt_f2i = Instruction(OP_CVT,{.cvt = CVT_F2I},
			Operand(REGISTER_VGPR, 3), Operand(REGISTER_VSP, VSP_TID_X));
		unsigned int i;
		bfloat intm;

		out_insn.write(op_cvt_i2f);
		out_col_w.write(3);
		for (i = 0; i < FPUS; i++)
			out_operand[0][i].write(i);

		wait();

		assert(in_col_mask_w.read() == 3);
		req = in_req_w.read();
		for (i = 0; i < FPUS; i++) {
			intm.b = in_data_w[i].read();
			assert(intm.f == i);
		}
		assert(op_cvt_i2f.getDst() == req);
		assert(req.col == 3);

		out_insn.write(op_cvt_f2i);
		out_col_w.write(3);
		for (i = 0; i < FPUS; i++) {
			intm.f = i;
			out_operand[0][i].write(intm.b);
		}

		wait();

		assert(in_col_mask_w.read() == 3);
		req = in_req_w.read();
		for (i = 0; i < FPUS; i++)
			assert(in_data_w[i].read() == i);
		assert(op_cvt_f2i.getDst() == req);
		assert(req.col == 3);
	}

	/** Control stack pop test. */
	void
	test_CPOP(void)
	{
		Instruction op = Instruction(OP_CPOP,{});
		ctrlstack_entry<THREADS,PC_WIDTH> cstack_entry;
		unsigned int i;

		for (i = 0; i < THREADS; i++) {
			if (i & FPUS)
				cstack_entry.pred_mask[i] = (i % 2) ? Log_0 : Log_1;
			else
				cstack_entry.pred_mask[i] = (i % 2) ? Log_1 : Log_0;
		}
		cstack_entry.pc = 7;
		cstack_entry.mask_type = VSP_CTRL_BREAK;

		out_cstack_top.write(cstack_entry);
		out_cstack_sp.write(1);
		out_insn.write(op);
		out_col_w.write(0);

		wait();
		wait(SC_ZERO_TIME);
		assert(in_ignore_mask_w.read() == 1);
		assert(in_cstack_action.read() == CTRLSTACK_IDLE);
		for (i = 0; i < FPUS; i++)
			assert(in_data_w[i].read() == ((i % 2) ? 1 : 0));

		op.setCommit(true);
		out_insn.write(op);
		out_col_w.write(1);

		wait();
		wait(SC_ZERO_TIME);
		assert(in_ignore_mask_w.read() == 1);
		assert(in_cstack_action.read() == CTRLSTACK_POP);
		for (i = 0; i < FPUS; i++)
			assert(in_data_w[i].read() == ((i % 2) ? 0 : 1));
		assert(in_pc_do_w.read() == true);
		assert(in_pc_w.read() == 7);

		op.kill();
		out_insn.write(op);

		/* Stall cycle, post-PC write. */
		wait();

		/* No mask should be read. Verify that there's nothing lingering
		 * in the fifo. */
		assert(in_col_mask_w.num_available() == 0);
	}

	/** Control stack push test. */
	void
	test_CPUSH(void)
	{
		Instruction op = Instruction(OP_CPUSH,{.cpush = CPUSH_BRK},{},Operand(4));
		ctrlstack_entry<THREADS,PC_WIDTH> cstack_entry;
		unsigned int i;

		for (i = 0; i < FPUS; i++)
			out_operand[1][i].write(i % 2);

		out_operand[0][0].write(4);

		op.setCommit(false);

		cstack_entry.pc = 7;
		cstack_entry.mask_type = VSP_CTRL_BREAK;

		out_cstack_top.write(cstack_entry);
		out_cstack_sp.write(1);
		out_insn.write(op);
		out_col_w.write(0);

		wait();
		wait(SC_ZERO_TIME);

		assert(in_cstack_action.read() == CTRLSTACK_IDLE);

		for (i = 0; i < FPUS; i++)
			out_operand[1][i].write((i+1) % 2);

		op.setCommit(true);
		out_insn.write(op);
		out_col_w.write(1);

		wait();
		wait(SC_ZERO_TIME);
		assert(in_cstack_action.read() == CTRLSTACK_PUSH);
		cstack_entry = in_cstack_entry.read();

		for (i = 0; i < FPUS * 2; i++) {
			if (i & FPUS)
				assert(cstack_entry.pred_mask[i].to_bool() == !(i % 2));
			else
				assert(cstack_entry.pred_mask[i].to_bool() == (i % 2));
		}

		for (; i < THREADS; i++)
			assert(!cstack_entry.pred_mask[i]);

		assert(cstack_entry.pc == 4);
		assert(cstack_entry.mask_type == VSP_CTRL_BREAK);

		op.kill();
		out_insn.write(op);

		wait();
		assert(in_col_mask_w.num_available() == 0);
	}

	void
	test_LDLIN(void)
	{
		Instruction op = Instruction(OP_LDGLIN,{.ldstlin=LIN_UNIT},Operand(REGISTER_VGPR,4),Operand(0));
		stride_descriptor sd;
		out_xlat_phys.write(Buffer(0x4000,1927,1080));
		out_operand[0][0].write(0);
		out_operand[1][0].write(0);
		out_operand[2][0].write(0);
		out_wg.write(0);
		out_insn.write(op);

		wait();
		wait(SC_ZERO_TIME);

		sd = in_desc_fifo[IF_DRAM].read();
		assert(in_store_kick[IF_DRAM].read());
		assert(in_wg_state_next[0].read() == WG_STATE_BLOCKED_DRAM);
		assert(sd.dst_period == 32);
		assert(sd.words == 7);
		assert(sd.addr == 0x7C9180);
		assert(sd.period_count == 24);
		assert(sd.period == 1927);
		assert(sd.dst_off_x == 0);
		assert(sd.dst_off_y == 0);
		assert(sd.dst_offset == 0);
		assert(in_pc_do_w.read());

		/** Flushing the pipeline, next instruction is ignored */
		out_insn.write(Instruction(NOP));
		wait();

		op = Instruction(OP_LDGLIN,{},Operand(REGISTER_VGPR,4), Operand(0));
		out_wg_off[0][0].write(0);
		out_wg_off[0][1].write(0);
		out_operand[0][0].write(0);
		out_operand[1][0].write(-1);
		out_operand[2][0].write(-1);
		out_insn.write(op);
		wait();
		wait(SC_ZERO_TIME);
		sd = in_desc_fifo[IF_DRAM].read();
		assert(in_store_kick[IF_DRAM].read());
		assert(in_wg_state_next[0].read() == WG_STATE_BLOCKED_DRAM);
		assert(sd.dst_period == 32);
		assert(sd.words == 31);
		assert(sd.addr == 0x4000);
		assert(sd.period_count == 31);
		assert(sd.period == 1927);
		assert(sd.dst_off_x == 1);
		assert(sd.dst_off_y == 1);
		assert(sd.dst_offset == 0);
		assert(in_pc_do_w.read());
	}

	/** Test MUL. */
	void
	test_RCP(void)
	{
		bfloat intm;
		bfloat res;
		Register<THREADS/FPUS> req;
		Instruction op = Instruction(OP_RCP,{},
			Operand(REGISTER_VGPR, 3), Operand(REGISTER_VGPR, 0));

		out_insn.write(op);
		out_col_w.write(2);
		for (unsigned int i = 0; i < FPUS; i++) {
			intm.f = i;
			out_operand[0][i].write(intm.b);
		}

		for (unsigned int i = 0; i < (FPUS/RCPUS); i++) {
			out_subcol_w.write(i);
			if (i == (FPUS/RCPUS) - 1) {
				op.setCommit(true);
				out_insn.write(op);
				break;
			}

			wait();
			assert(!in_w.read());
		}

		wait();
		assert(in_col_mask_w.read() == 2);
		assert(in_w.read());
		req = in_req_w.read();
		for (unsigned int i = 0; i < FPUS; i++) {
			res.b = in_data_w[i].read();
			intm.f = 1.f / i;
			assert(intm.b == res.b);
		}
		assert(op.getDst() == req);
		assert(req.col == 2);
	}

	void
	test_SIBFIND(void)
	{
		unsigned int intm;
		unsigned int res;
		Register<THREADS/FPUS> req;
		Instruction op = Instruction(OP_SIBFIND,{},
			Operand(REGISTER_SGPR, 3), Operand(REGISTER_SGPR, 0));

		intm = 32;
		out_insn.write(op);
		out_col_w.write(0);
		out_operand[0][0].write(intm);
		out_subcol_w.write(0);

		wait();
		wait(SC_ZERO_TIME);
		assert(in_w.read());
		req = in_req_w.read();
		res = in_data_w[0].read();
		assert(res == 5);
		assert(op.getDst() == req);
		assert(req.col == 0);

		intm = 0;
		out_insn.write(op);
		out_col_w.write(0);
		out_operand[0][0].write(intm);
		out_subcol_w.write(0);

		wait();
		wait(SC_ZERO_TIME);
		assert(in_w.read());
		req = in_req_w.read();
		res = in_data_w[0].read();
		assert(res == ~0);
		assert(op.getDst() == req);
		assert(req.col == 0);

		intm = 127;
		out_insn.write(op);
		out_col_w.write(0);
		out_operand[0][0].write(intm);
		out_subcol_w.write(0);

		wait();
		wait(SC_ZERO_TIME);
		assert(in_w.read());
		req = in_req_w.read();
		res = in_data_w[0].read();
		assert(res == 6);
		assert(op.getDst() == req);
		assert(req.col == 0);
	}

	/** Main thread.
	 * @todo Randomised testing, to verify sequences of ops. */
	void
	thread_lt(void)
	{
		sc_bv<2> thread_active;

		out_wg.write(0);
		thread_active[0] = Log_1;
		thread_active[1] = Log_1;
		out_thread_active.write(thread_active);

		out_wg_width.write(WG_WIDTH_32);
		out_dim[0].write(1927);
		out_dim[1].write(1080);
		out_wg_off[0][0].write(60);
		out_wg_off[0][1].write(1056);
		wait();

		test_MAD();
		test_MUL();
		test_ADD();

		test_TEST();
		test_PBOOL();

		test_MOV();
		test_CVT();

		test_CMASK();
		test_CPOP();
		test_CPUSH();

		test_LDLIN();
		test_RCP();

		test_SIBFIND();

		test_finish();
	}
};

}

using namespace compute_control;
using namespace compute_test;

int
sc_main(int argc, char* argv[])
{
	sc_signal<sc_uint<11> > pc;
	sc_signal<Instruction> insn;
	sc_signal<bool> insn_valid;
	sc_signal<sc_uint<1> > wg;
	sc_signal<sc_uint<const_log2(COMPUTE_THREADS/COMPUTE_FPUS)> > col_w;
	sc_signal<sc_uint<const_log2(COMPUTE_FPUS/COMPUTE_RCPUS)> > subcol_w;
	sc_signal<sc_uint<32> > operand[3][COMPUTE_FPUS];
	sc_signal<sc_bv<2> > thread_active;
	sc_signal<sc_uint<11> > pc_w;
	sc_signal<bool> pc_do_w;
	sc_signal<Register<COMPUTE_THREADS/COMPUTE_FPUS> > req_w;
	sc_signal<sc_uint<32> > data_w[COMPUTE_FPUS];
	sc_signal<bool> w;
	sc_signal<bool> dequeue_sb;
	sc_signal<bool> ignore_mask_w;
	sc_signal<sc_uint<1> > wg_w;
	sc_fifo<sc_uint<const_log2(COMPUTE_THREADS/COMPUTE_FPUS)> >
				col_mask_w(1);
	sc_signal<ctrlstack_action> cstack_action;
	sc_signal<ctrlstack_entry<COMPUTE_THREADS,11> > cstack_entry;
	sc_signal<ctrlstack_entry<COMPUTE_THREADS,11> > cstack_top;
	sc_signal<sc_uint<5> > cstack_sp;
	sc_signal<bool> cstack_full;
	sc_signal<bool> cstack_ex_overflow;
	sc_signal<bool> dequeue_sb_cstack_write;
	sc_signal<Buffer> xlat_phys;
	sc_signal<Buffer> sp_xlat_phys;
	sc_signal<sc_uint<32> > wg_off[2][2];
	sc_signal<sc_uint<32> > dim[2];
	sc_signal<workgroup_width> wg_width;
	sc_fifo<stride_descriptor> desc_fifo(2);
	sc_fifo<stride_descriptor> desc_fifo_wg0(2);
	sc_fifo<stride_descriptor> desc_fifo_wg1(2);
	sc_fifo<bool> store_kick[IF_SENTINEL];
	sc_signal<workgroup_state> wg_state_next[2];
	sc_signal<sc_bv<2> > wg_exit_commit;
	sc_signal<stride_descriptor> sd[2];

	sc_clock clk("clk", sc_time(10./12., SC_NS));

	IExecute<11,COMPUTE_THREADS,COMPUTE_FPUS,COMPUTE_RCPUS> my_iexecute("my_iexecute");
	my_iexecute.in_clk(clk);
	my_iexecute.in_pc(pc);
	my_iexecute.in_insn(insn);
	my_iexecute.in_wg(wg);
	my_iexecute.in_col_w(col_w);
	my_iexecute.in_subcol_w(subcol_w);
	my_iexecute.in_sd[0](sd[0]);
	my_iexecute.in_sd[1](sd[1]);
	my_iexecute.in_thread_active(thread_active);
	my_iexecute.in_xlat_phys(xlat_phys);
	my_iexecute.in_sp_xlat_phys(sp_xlat_phys);
	my_iexecute.out_pc_w(pc_w);
	my_iexecute.out_pc_do_w(pc_do_w);
	my_iexecute.out_req_w(req_w);
	my_iexecute.out_wg_w(wg_w);
	my_iexecute.out_w(w);
	my_iexecute.out_dequeue_sb(dequeue_sb);
	my_iexecute.out_ignore_mask_w(ignore_mask_w);
	my_iexecute.out_col_mask_w(col_mask_w);
	my_iexecute.out_cstack_action(cstack_action);
	my_iexecute.out_cstack_entry(cstack_entry);
	my_iexecute.in_cstack_top(cstack_top);
	my_iexecute.in_cstack_sp(cstack_sp);
	my_iexecute.in_cstack_full(cstack_full);
	my_iexecute.in_cstack_ex_overflow(cstack_ex_overflow);
	my_iexecute.out_dequeue_sb_cstack_write(dequeue_sb_cstack_write);
	my_iexecute.in_wg_off[0][0](wg_off[0][0]);
	my_iexecute.in_wg_off[0][1](wg_off[0][1]);
	my_iexecute.in_wg_off[1][0](wg_off[1][0]);
	my_iexecute.in_wg_off[1][1](wg_off[1][1]);
	my_iexecute.in_dim[0](dim[0]);
	my_iexecute.in_dim[1](dim[1]);
	my_iexecute.in_wg_width(wg_width);
	my_iexecute.out_desc_fifo[IF_DRAM](desc_fifo);
	my_iexecute.out_desc_fifo[IF_SP_WG0](desc_fifo_wg0);
	my_iexecute.out_desc_fifo[IF_SP_WG1](desc_fifo_wg1);
	my_iexecute.out_store_kick[IF_DRAM](store_kick[IF_DRAM]);
	my_iexecute.out_store_kick[IF_SP_WG0](store_kick[IF_SP_WG0]);
	my_iexecute.out_store_kick[IF_SP_WG1](store_kick[IF_SP_WG1]);
	my_iexecute.out_wg_state_next[0](wg_state_next[0]);
	my_iexecute.out_wg_state_next[1](wg_state_next[1]);
	my_iexecute.out_wg_exit_commit(wg_exit_commit);

	Test_IExecute<11,COMPUTE_THREADS,COMPUTE_FPUS,COMPUTE_RCPUS> my_iexecute_test("my_iexecute_test");
	my_iexecute_test.in_clk(clk);
	my_iexecute_test.out_pc(pc);
	my_iexecute_test.out_insn(insn);
	my_iexecute_test.out_wg(wg);
	my_iexecute_test.out_col_w(col_w);
	my_iexecute_test.out_subcol_w(subcol_w);
	my_iexecute_test.out_sd[0](sd[0]);
	my_iexecute_test.out_sd[1](sd[1]);
	my_iexecute_test.out_thread_active(thread_active);
	my_iexecute_test.out_xlat_phys(xlat_phys);
	my_iexecute_test.out_sp_xlat_phys(sp_xlat_phys);
	my_iexecute_test.in_pc_w(pc_w);
	my_iexecute_test.in_pc_do_w(pc_do_w);
	my_iexecute_test.in_req_w(req_w);
	my_iexecute_test.in_w(w);
	my_iexecute_test.in_dequeue_sb(dequeue_sb);
	my_iexecute_test.in_ignore_mask_w(ignore_mask_w);
	my_iexecute_test.in_wg_w(wg_w);
	my_iexecute_test.in_col_mask_w(col_mask_w);
	my_iexecute_test.in_cstack_action(cstack_action);
	my_iexecute_test.in_cstack_entry(cstack_entry);
	my_iexecute_test.out_cstack_top(cstack_top);
	my_iexecute_test.out_cstack_sp(cstack_sp);
	my_iexecute_test.out_cstack_full(cstack_full);
	my_iexecute_test.out_cstack_ex_overflow(cstack_ex_overflow);
	my_iexecute_test.in_dequeue_sb_cstack_write(dequeue_sb_cstack_write);
	my_iexecute_test.out_wg_off[0][0](wg_off[0][0]);
	my_iexecute_test.out_wg_off[0][1](wg_off[0][1]);
	my_iexecute_test.out_wg_off[1][0](wg_off[1][0]);
	my_iexecute_test.out_wg_off[1][1](wg_off[1][1]);
	my_iexecute_test.out_dim[0](dim[0]);
	my_iexecute_test.out_dim[1](dim[1]);
	my_iexecute_test.out_wg_width(wg_width);
	my_iexecute_test.in_desc_fifo[IF_DRAM](desc_fifo);
	my_iexecute_test.in_desc_fifo[IF_SP_WG0](desc_fifo_wg0);
	my_iexecute_test.in_desc_fifo[IF_SP_WG1](desc_fifo_wg1);
	my_iexecute_test.in_store_kick[IF_DRAM](store_kick[IF_DRAM]);
	my_iexecute_test.in_store_kick[IF_SP_WG0](store_kick[IF_SP_WG0]);
	my_iexecute_test.in_store_kick[IF_SP_WG1](store_kick[IF_SP_WG1]);
	my_iexecute_test.in_wg_state_next[0](wg_state_next[0]);
	my_iexecute_test.in_wg_state_next[1](wg_state_next[1]);
	my_iexecute_test.in_wg_exit_commit(wg_exit_commit);

	for (unsigned int i = 0; i < COMPUTE_FPUS; i++) {
		my_iexecute.out_data_w[i](data_w[i]);
		my_iexecute_test.in_data_w[i](data_w[i]);

		for (unsigned int p = 0; p < 3; p++) {
			my_iexecute.in_operand[p][i](operand[p][i]);
			my_iexecute_test.out_operand[p][i](operand[p][i]);
		}
	}

	sc_core::sc_start(850, sc_core::SC_NS);

	assert(my_iexecute_test.has_finished());

	return 0;
}
