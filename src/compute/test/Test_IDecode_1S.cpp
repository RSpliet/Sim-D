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

#include "util/SimdTest.h"
#include "util/defaults.h"
#include "compute/control/IDecode_1S.h"

using namespace sc_core;
using namespace sc_dt;
using namespace compute_model;
using namespace compute_control;
using namespace simd_test;
using namespace isa_model;

namespace compute_test {

/* XXX: Write some macros for shorter notation... */
static Instruction op_vec[]{
		Instruction(OP_MAD,{},Operand(REGISTER_VGPR,0),Operand(REGISTER_VGPR,0),Operand(REGISTER_VGPR,1),Operand(REGISTER_VGPR,2)),
		Instruction(OP_MUL,{},Operand(REGISTER_VGPR,0),Operand(REGISTER_VGPR,0),Operand(4)),
		Instruction(OP_ADD,{},Operand(REGISTER_VGPR,0),Operand(REGISTER_VGPR,0),Operand(REGISTER_SGPR,2)),
		Instruction(OP_MOV,{},Operand(REGISTER_VGPR,0),Operand(1)),
		Instruction(OP_CVT,{.cvt = CVT_I2F},Operand(REGISTER_VGPR,3),Operand(REGISTER_VSP,1)),
		Instruction(OP_TEST,{.test = TEST_NZ},Operand(REGISTER_PR,0),Operand(REGISTER_VGPR,3)),
		Instruction(OP_BRK,{},{},Operand(REGISTER_PR,0)),
		Instruction(OP_EXIT,{},{},Operand(REGISTER_PR,2)),
		Instruction(OP_RET,{},{},Operand(REGISTER_PR,3)),
		Instruction(OP_CPOP,{}),
		Instruction(OP_CPUSH,{.cpush = CPUSH_BRK},{},Operand(8)),
		Instruction(OP_PBOOL, {.pbool = PBOOL_AND},Operand(REGISTER_PR,0),Operand(REGISTER_PR,0),Operand(REGISTER_PR,1)),
		Instruction(OP_RCP,{},Operand(REGISTER_VGPR,0),Operand(REGISTER_VGPR,0)),
};
static Instruction op_scalar[]{
		Instruction(NOP,{}),
		Instruction(OP_J,{},{},Operand(8)),
		Instruction(OP_J,{},{},Operand(4)),
};

/** Unit test for compute_control::IDecode_1S_3R1W. */
template <unsigned int PC_WIDTH, unsigned int THREADS = 1024,
		unsigned int FPUS = 128, unsigned int RCPUS = 32,
		unsigned int XLAT_ENTRIES = 32>
class Test_IDecode_1S : public SimdTest
{
public:
	/** Compute clock. */
	sc_in<bool> in_clk{"in_clk"};

	/** Instruction fetched by IFetch. */
	sc_inout<Instruction> out_insn{"out_insn"};

	/** Program counter associated with out_insn. */
	sc_inout<sc_uint<PC_WIDTH> > out_pc{"out_pc"};

	/** Currently active workgroup. */
	sc_inout<sc_uint<1> > out_wg{"out_wg"};

	/** Width of workgroups for running kernel. */
	sc_inout<workgroup_width> out_wg_width{"out_wg_width"};

	/** Identifier of last warp (number of active warps - 1). */
	sc_inout<sc_uint<const_log2(THREADS/FPUS)> >
				out_last_warp[2];

	/** True if a cstack pop should be issued. PC will change, so IMem
	 * input can be disregarded as invalid. */
	sc_inout<sc_bv<2> > out_thread_active{"out_thread_active"};

	/** Finished bit, comes slightly earlier than state. */
	sc_inout<sc_bv<2> > out_wg_finished{"out_wg_finished"};

	sc_in<sc_uint<PC_WIDTH> > in_pc{"in_pc"};

	/** Instruction relayed by IDecode. */
	sc_in<Instruction> in_insn{"in_insn"};

	/** Read requests to the regfile. Async. */
	sc_fifo_in<reg_read_req<THREADS/FPUS> > in_req{"in_req"};

	/** Read requests mirrored to the scoreboard. Async. */
	sc_fifo_in<reg_read_req<THREADS/FPUS> > in_req_sb{"in_req_sb"};

	sc_in<bool> in_ssp_match{"in_ssp_match"};

	/** Enqueue the entry just added to out_req_sb. */
	sc_in<bool> in_enqueue_sb{"in_enqueue_sb"};

	/** Enqueue a control stack write to the scoreboard. */
	sc_in<bool> in_enqueue_sb_cstack_write
				{"in_enqueue_sb_cstack_write"};

	/** Enqueue a control stack write to the scoreboard. */
	sc_in<sc_uint<1> > in_enqueue_sb_cstack_wg
				{"in_enqueue_sb_cstack_wg"};

	/** True iff CPOPs must stall (until all scoreboard entries have been
	 * committed). */
	sc_inout<bool> out_sb_cpop_stall[2];

	/** Read requests mirrored to the scoreboard. Async. */
	sc_in<Register<THREADS/FPUS> > in_req_w_sb{"in_req_w_sb"};

	/** Population bitmask of SB entries. */
	sc_inout<sc_bv<32> > out_entries_pop[2];

	/** Currently active workgroup. */
	sc_in<sc_uint<1> > in_wg{"in_wg"};

	/** Column to write result of instruction to. Used for IExecute and
	 * write mask retrieval. */
	sc_in<sc_uint<const_log2(THREADS/FPUS)> > in_col_w{"in_col_w"};

	/** Column to write result of instruction to. Used for IExecute and
	 * write mask retrieval. */
	sc_in<sc_uint<const_log2(FPUS/RCPUS)> > in_subcol_w{"in_subcol_w"};

	/** Stall fetch signal e.g. on vector instructions that require
	 * multiple warps to execute. */
	sc_in<bool> in_stall_f{"in_stall_f"};

	/** Stall for RAW hazard. */
	sc_fifo_out<sc_bv<3> > out_raw{"out_raw"};

	sc_fifo_out<sc_bv<3> > out_req_conflicts{"out_req_conflicts"};

	/** Trigger a pipeline flush e.g. on a branch. */
	sc_inout<bool> out_pipe_flush{"out_pipe_flush"};

	/** BufferToPhysXlat read index */
	sc_in<sc_uint<const_log2(XLAT_ENTRIES)> > in_xlat_idx{"in_xlat_idx"};

	/** BufferToPhysXlat read index */
	sc_in<sc_uint<const_log2(XLAT_ENTRIES)> >
				in_sp_xlat_idx{"in_sp_xlat_idx"};

	/** Construct test thread */
	SC_CTOR(Test_IDecode_1S)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();
	}
private:
	/** Validate the commit bit of an instruction, and correct the expected
	 * instructions commit bit such that the comparator returns true.
	 * @param thisOp Expected (golden) instruction
	 * @param op Instruction retreived from IDecode
	 * @param commit True iff commit is expected, false otherwise.
	 */
	void
	validate_commit(Instruction &thisOp, Instruction op, bool commit)
	{
		bool on_cstack_sb;
		assert(op.getCommit() == commit);

		on_cstack_sb = (op.doesCPUSH() && op.getCommit());
		assert(op.getOnCStackSb() == on_cstack_sb);
		thisOp.setOnCStackSb(on_cstack_sb);

		thisOp.setCommit(commit);
	}

	/** CPUSH issues an extra read from the special register file. Check
	 * whether it's valid. */
	void
	validate_CPUSH(Instruction &thisOp, Instruction op)
	{
		ISASubOpCPUSH subop = thisOp.getSubOp().cpush;

		switch (subop) {
		case CPUSH_IF:
			thisOp.addSrc(Operand(REGISTER_VSP,VSP_CTRL_RUN));
			break;
		case CPUSH_BRK:
			thisOp.addSrc(Operand(REGISTER_VSP,VSP_CTRL_BREAK));
			break;
		case CPUSH_RET:
			thisOp.addSrc(Operand(REGISTER_VSP,VSP_CTRL_RET));
			break;
		default:
			assert(false);
			break;
		}
	}

	/** Generic verification of instruction.
	 * @param thisOp Expected instruction.
	 * @param pc Program counter value. Sort-of-arbitrary.
	 * @param last_warp Identifier of last warp. 0 for scalar.*/
	void
	test_n_insn(Instruction thisOp, unsigned int pc, unsigned int last_warp)
	{
		unsigned int i;
		unsigned int src;
		reg_read_req<THREADS/FPUS> req;
		Instruction op;
		Instruction cmpOp = thisOp;
		unsigned int col, subcol;
		unsigned int lw;

		out_insn.write(thisOp);
		out_pc.write(pc);

		switch (cmpOp.getOp()) {
		case OP_CPUSH:
			validate_CPUSH(cmpOp, op);
			break;
		case OP_BRK:
			cmpOp.setDst(Operand(REGISTER_VSP,VSP_CTRL_BREAK));
			break;
		case OP_EXIT:
			cmpOp.setDst(Operand(REGISTER_VSP,VSP_CTRL_EXIT));
			break;
		case OP_CMASK:
		case OP_BRA:
			cmpOp.setDst(Operand(REGISTER_VSP,VSP_CTRL_RUN));
			break;
		case OP_RET:
			cmpOp.setDst(Operand(REGISTER_VSP,VSP_CTRL_RET));
			break;
		default:
			break;
		}

		if (opCategory(cmpOp.getOp()) == CAT_ARITH_RCPU)
			lw = ((last_warp + 1) * (FPUS/RCPUS)) - 1;
		else
			lw = last_warp;

		for (i = 0; i <= lw; i++) {
			wait();

			/* This order is very important. in_req.read() is a
			 * FIFO read that will block until the next delta cycle,
			 * at which point op will have been updated too. */
			req = in_req.read();
			in_req_sb.read();
			out_req_conflicts.write(0);
			out_raw.write(0);
			wait(SC_ZERO_TIME);
			wait(SC_ZERO_TIME);

			op = in_insn.read();

			cout << op << " ?= " << cmpOp << endl;

			if (opCategory(op.getOp()) == CAT_ARITH_RCPU) {
				col = i / (FPUS/RCPUS);
				subcol = i % (FPUS/RCPUS);

				if (subcol == (FPUS/RCPUS) - 1) {
					validate_commit(cmpOp, op, true);
					cmpOp.setOnSb(true);
				} else {
					validate_commit(cmpOp, op, false);
					cmpOp.setOnSb(false);
				}
			} else {
				col = i;
				subcol = 0;

				if (cmpOp.hasDst() && !cmpOp.ldst())
					cmpOp.setOnSb(true);
				else
					cmpOp.setOnSb(false);

				if (op.getOp() == OP_CPUSH || op.writesCMASK())
					validate_commit(cmpOp, op, (i == last_warp));
			}

			assert(op == cmpOp);

			for (src = 0; src < cmpOp.getSrcs(); src++) {
				if (subcol == 0)
					assert(req.r[src]);
				assert(cmpOp.getSrc(src) == req.reg[src]);

				if (cmpOp.getSrc(src).isVectorType())
					assert(req.reg[src].col == col);
				else
					assert(req.reg[src].col == 0);
			}

			assert(in_col_w.read() == col);
			assert(in_subcol_w.read() == subcol);

			if (i == lw)
				assert(!in_stall_f.read());
			else
				assert(in_stall_f.read());

			cout << sc_time_stamp() << hex << " " << i << ": " <<
					dec << thisOp << endl;
		}
	}

	/** Iterate over all vector instructions in our test set. */
	void
	test_vector_insn(void)
	{
		unsigned int o;
		const unsigned int entries = sizeof(op_vec)/sizeof(op_vec[0]);

		for (o = 0; o < entries; o++) {
			test_n_insn(op_vec[o], o, 3);
		}
	}

	/** Iterate over all scalar instructions in our test set. */
	void
	test_scalar_insns(void)
	{
		unsigned int o;
		const unsigned int entries = sizeof(op_scalar)/
				sizeof(op_scalar[0]);

		for (o = 0; o < entries; o++) {
			test_n_insn(op_scalar[o], o, 0);
		}
	}

	/** Test the instruction kill bit being set on a pipeline flush. */
	void
	test_kill(void)
	{
		Instruction op;
		reg_read_req<THREADS/FPUS> req;

		out_pipe_flush.write(true);
		out_insn.write(Instruction(NOP,{}));
		out_pc.write(0);

		wait();

		req = in_req.read();
		in_req_sb.read();
		out_req_conflicts.write(0);
		out_raw.write(0);
		wait(SC_ZERO_TIME);
		wait(SC_ZERO_TIME);

		op = in_insn.read();
		out_pipe_flush.write(false);

		assert(op.isDead());
	}

	/** Main thread.
	 * @todo Conflicts when two ports request same register? */
	void
	thread_lt(void)
	{
		sc_bv<2> thread_active;
		sc_bv<2> wg_finished = 0;

		thread_active[0] = Log_1;
		thread_active[1] = Log_1;

		out_thread_active.write(thread_active);
		out_last_warp[0].write(3);
		out_wg_finished.write(wg_finished);
		out_entries_pop[0].write(0);
		out_entries_pop[1].write(0);
		out_wg_width.write(WG_WIDTH_128);

		test_vector_insn();
		test_scalar_insns();
		test_kill();

		test_finish();
	}
};

}

using namespace compute_control;
using namespace compute_test;

int
sc_main(int argc, char* argv[])
{
	sc_signal<Instruction> insn;
	sc_signal<sc_uint<11> > pc;
	sc_signal<sc_uint<1> > iwarp;
	sc_signal<workgroup_width> wg_width;
	sc_signal<sc_uint<const_log2(COMPUTE_THREADS/COMPUTE_FPUS)> > last_warp[2];
	sc_signal<sc_bv<2> > thread_active;
	sc_signal<sc_bv<2> > wg_finished;
	sc_signal<sc_uint<11> > o_pc;
	sc_signal<Instruction> o_insn;
	sc_signal<bool> insn_valid;
	sc_fifo<reg_read_req<COMPUTE_THREADS/COMPUTE_FPUS> > req(1);
	sc_fifo<reg_read_req<COMPUTE_THREADS/COMPUTE_FPUS> > req_sb(1);
	sc_signal<bool> ssp_match;
	sc_signal<bool> enqueue_sb;
	sc_signal<bool> enqueue_sb_cstack_write;
	sc_signal<sc_uint<1> > enqueue_sb_cstack_wg;
	sc_signal<bool> sb_cpop_stall[2];
	sc_signal<Register<COMPUTE_THREADS/COMPUTE_FPUS> > req_w_sb;
	sc_signal<sc_bv<32> > entries_pop[2];
	sc_signal<sc_uint<1> > o_warp;
	sc_signal<sc_uint<const_log2(COMPUTE_THREADS/COMPUTE_FPUS)> > col_w;
	sc_signal<sc_uint<const_log2(COMPUTE_FPUS/COMPUTE_RCPUS)> > subcol_w;
	sc_signal<bool> stall_f, pipe_flush;
	sc_fifo<sc_bv<3> > raw(1);
	sc_fifo<sc_bv<3> > req_conflicts(1);
	sc_signal<sc_uint<const_log2(MC_BIND_BUFS)> > xlat_idx;
	sc_signal<sc_uint<const_log2(MC_BIND_BUFS)> > sp_xlat_idx;

	sc_clock clk("clk", sc_time(10./12., SC_NS));

	IDecode_1S<11,COMPUTE_THREADS,COMPUTE_FPUS,COMPUTE_RCPUS,MC_BIND_BUFS>
			my_idecode("my_idecode");
	my_idecode.in_clk(clk);
	my_idecode.in_insn(insn);
	my_idecode.in_pc(pc);
	my_idecode.in_wg(iwarp);
	my_idecode.in_wg_width(wg_width);
	my_idecode.in_last_warp[0](last_warp[0]);
	my_idecode.in_last_warp[1](last_warp[1]);
	my_idecode.in_thread_active(thread_active);
	my_idecode.in_wg_finished(wg_finished);
	my_idecode.out_pc(o_pc);
	my_idecode.out_insn(o_insn);
	my_idecode.out_req(req);
	my_idecode.out_req_sb(req_sb);
	my_idecode.out_ssp_match(ssp_match);
	my_idecode.in_entries_pop[0](entries_pop[0]);
	my_idecode.in_entries_pop[1](entries_pop[1]);
	my_idecode.out_enqueue_sb(enqueue_sb);
	my_idecode.out_enqueue_sb_cstack_write(enqueue_sb_cstack_write);
	my_idecode.out_enqueue_sb_cstack_wg(enqueue_sb_cstack_wg);
	my_idecode.in_sb_cpop_stall[0](sb_cpop_stall[0]);
	my_idecode.in_sb_cpop_stall[1](sb_cpop_stall[1]);
	my_idecode.out_req_w_sb(req_w_sb);
	my_idecode.out_wg(o_warp);
	my_idecode.out_col_w(col_w);
	my_idecode.out_subcol_w(subcol_w);
	my_idecode.out_stall_f(stall_f);
	my_idecode.in_raw(raw);
	my_idecode.in_req_conflicts(req_conflicts);
	my_idecode.in_pipe_flush(pipe_flush);
	my_idecode.out_xlat_idx(xlat_idx);
	my_idecode.out_sp_xlat_idx(sp_xlat_idx);

	Test_IDecode_1S<11,COMPUTE_THREADS,COMPUTE_FPUS,COMPUTE_RCPUS,MC_BIND_BUFS>
			my_idecode_test("my_idecode_test");
	my_idecode_test.in_clk(clk);
	my_idecode_test.out_insn(insn);
	my_idecode_test.out_pc(pc);
	my_idecode_test.out_wg(iwarp);
	my_idecode_test.out_wg_width(wg_width);
	my_idecode_test.out_last_warp[0](last_warp[0]);
	my_idecode_test.out_last_warp[1](last_warp[1]);
	my_idecode_test.out_thread_active(thread_active);
	my_idecode_test.out_wg_finished(wg_finished);
	my_idecode_test.in_pc(o_pc);
	my_idecode_test.in_insn(o_insn);
	my_idecode_test.in_req(req);
	my_idecode_test.in_req_sb(req_sb);
	my_idecode_test.in_ssp_match(ssp_match);
	my_idecode_test.out_entries_pop[0](entries_pop[0]);
	my_idecode_test.out_entries_pop[1](entries_pop[1]);
	my_idecode_test.in_enqueue_sb(enqueue_sb);
	my_idecode_test.in_enqueue_sb_cstack_write(enqueue_sb_cstack_write);
	my_idecode_test.in_enqueue_sb_cstack_wg(enqueue_sb_cstack_wg);
	my_idecode_test.out_sb_cpop_stall[0](sb_cpop_stall[0]);
	my_idecode_test.out_sb_cpop_stall[1](sb_cpop_stall[1]);
	my_idecode_test.in_req_w_sb(req_w_sb);
	my_idecode_test.in_wg(o_warp);
	my_idecode_test.in_col_w(col_w);
	my_idecode_test.in_subcol_w(subcol_w);
	my_idecode_test.in_stall_f(stall_f);
	my_idecode_test.out_raw(raw);
	my_idecode_test.out_req_conflicts(req_conflicts);
	my_idecode_test.out_pipe_flush(pipe_flush);
	my_idecode_test.in_xlat_idx(xlat_idx);
	my_idecode_test.in_sp_xlat_idx(sp_xlat_idx);

	sc_core::sc_start(700, sc_core::SC_NS);

	assert(my_idecode_test.has_finished());

	return 0;
}
