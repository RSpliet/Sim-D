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

#ifndef COMPUTE_CONTROL_IDECODE_H
#define COMPUTE_CONTROL_IDECODE_H

#include <systemc>

#include "model/reg_read_req.h"
#include "compute/model/compute_stats.h"
#include "isa/model/Instruction.h"
#include "model/workgroup_width.h"
#include "util/constmath.h"
#include "util/debug_output.h"

using namespace std;
using namespace sc_dt;
using namespace sc_core;
using namespace compute_model;
using namespace isa_model;

namespace compute_control {

/** Chosen implementation of IDecode. */
typedef enum {
	IDECODE_1S, /**< Single stage, 3R1W IDecode */
	IDECODE_3S  /**< Three-stage, one operand per-warp per-cycle */
} IDecode_impl;

/** Instruction decode pipeline stage.
 * @param PC_WIDTH Number of bits in a PC.
 * @param THREADS Maximum number of warps in a work-group.
 * @param FPUS number of FPUs/lanes in the execute phase.
 * @param RCPUS number of reciprocal units.
 * @param XLAT_ENTRIES Maximum number of buffers that can be mapped.
 */
template <unsigned int PC_WIDTH, unsigned int THREADS = 1024,
		unsigned int FPUS = 128, unsigned int RCPUS = 32,
		unsigned int XLAT_ENTRIES = 32>
class IDecode : public sc_module
{
protected:
	/** Currently active warp counter for vector instructions */
	sc_uint<const_log2(THREADS/RCPUS)> active_warp;

	/** Stall due to RAW hazard. */
	unsigned long raw_stalls;

	/** Stalls due to bank conflicts. */
	unsigned long read_bank_conflict_stalls;

	/** Stalls due to resource (e.g. integer divider) being busy. */
	unsigned long resource_busy_stalls;

	/** Vector instructions (and otherwise) need to be expanded. Cache the
	 * number of repeats (-1) in this variable. */
	unsigned int last_warp;

	/** Cycle counter for sidiv pipeline stall.
	 *
	 * Need to stall for 8 - IExecute.pipe_length() cycles to guarantee
	 * in-order execution. */
	int sidiv_pipe_stall;

	/** Counter to safeguard distance between two sidiv/simod insns. */
	int sidiv_issue_dist_stall;

	/** Number of IExecute pipeline stages. Used for resource busy stall
	 * counters. */
	unsigned int iexec_pipeline_stages;

	/** Boolean indicating whether a CPOP can be injected. Used to prevent
	 * double-injection. */
	bool cpop_can_inject;

public:
	/** Compute clock. */
	sc_in<bool> in_clk{"in_clk"};

	/** Instruction fetched by IFetch. */
	sc_in<Instruction> in_insn{"in_insn"};

	/** PC accompanying the instruction at in_insn. */
	sc_in<sc_uint<PC_WIDTH> > in_pc{"in_pc"};

	/** Currently active workgroup. */
	sc_in<sc_uint<1> > in_wg{"in_wg"};

	/** Width of each work group. */
	sc_in<workgroup_width> in_wg_width{"in_wg_width"};

	/** Identifier of last warp (number of active warps - 1). */
	sc_in<sc_uint<const_log2(THREADS/FPUS)> > in_last_warp[2];

	/** True if a cstack pop should be issued. PC will change, so IMem
	 * input can be disregarded as invalid. */
	sc_in<sc_bv<2> > in_thread_active{"in_thread_active"};

	/** Finished bit, comes slightly earlier than state. */
	sc_in<sc_bv<2> > in_wg_finished{"in_wg_finished"};

	/** Pass PC down the pipeline. */
	sc_inout<sc_uint<PC_WIDTH> > out_pc{"out_pc"};

	/** Instruction fetched by IFetch. */
	sc_inout<Instruction> out_insn{"out_insn"};

	/** Read requests to the regfile. Async. */
	sc_fifo_out<reg_read_req<THREADS/FPUS> > out_req{"out_req"};

	/** Read requests mirrored to the scoreboard. Async. */
	sc_fifo_out<reg_read_req<THREADS/FPUS> > out_req_sb{"out_req_sb"};

	/** True iff the request in pipeline stage 0 must block on SSP writes.*/
	sc_inout<bool> out_ssp_match{"out_ssp_match"};

	/** Enqueue the entry just added to out_req_sb. */
	sc_inout<bool> out_enqueue_sb{"out_enqueue_sb"};

	/** Enqueue a control stack write to the scoreboard. */
	sc_inout<bool> out_enqueue_sb_cstack_write
				{"out_enqueue_sb_cstack_write"};

	/** Enqueue a control stack write to the scoreboard. */
	sc_inout<sc_uint<1> > out_enqueue_sb_cstack_wg
				{"out_enqueue_sb_cstack_wg"};

	/** True iff CPOPs must stall (until all scoreboard entries have been
	 * committed). */
	sc_in<bool> in_sb_cpop_stall[2];

	/** Read requests mirrored to the scoreboard. Async. */
	sc_inout<Register<THREADS/FPUS> > out_req_w_sb{"out_req_w_sb"};

	/** Bitmap of currently populated Scoreboard CAM slots. */
	sc_in<sc_bv<32> > in_entries_pop[2];

	/** Currently active workgroup. */
	sc_inout<sc_uint<1> > out_wg{"out_wg"};

	/** Column to write result of instruction to. Used for IExecute and
	 * write mask retrieval. */
	sc_inout<sc_uint<const_log2(THREADS/FPUS)> > out_col_w{"out_col_w"};

	/** Subcolumn to write results of instructions to. Used for SFU
	 * operations, of which fewer are present than FPUs. */
	sc_inout<sc_uint<const_log2(FPUS/RCPUS)> > out_subcol_w{"out_subcol_w"};

	/** Stall signal for fetch.
	 *
	 * Triggered under two conditions:
	 * - Enumerating the warps of a vector instruction.
	 * - RAW hazard, indicated by the scoreboard. */
	sc_inout<bool> out_stall_f{"out_stall_f"};

	/** Stall for RAW hazard. */
	sc_fifo_in<sc_bv<3> > in_raw{"in_raw"};

	/** Bank conflicts determined by the scoreboard. */
	sc_fifo_in<sc_bv<3> > in_req_conflicts{"in_req_conflicts"};

	/** Trigger a pipeline flush e.g. on a branch. */
	sc_in<bool> in_pipe_flush{"in_pipe_flush"};

	/** Request DRAM buffer specification. */
	sc_out<sc_uint<const_log2(XLAT_ENTRIES)> > out_xlat_idx{"out_xlat_idx"};

	/** Request scratchpad buffer specification. */
	sc_out<sc_uint<const_log2(XLAT_ENTRIES)> >
				out_sp_xlat_idx{"out_sp_xlat_idx"};

	/** Fill the provided compute_stats object with aggregate stats.
	 * @param s Reference to the compute_stats object to update. */
	void
	get_stats(compute_stats &s)
	{
		s.raw_stalls = raw_stalls;
		s.rf_bank_conflict_stalls = read_bank_conflict_stalls;
		s.resource_busy_stalls = resource_busy_stalls;
	}

	/** Return the number of IDecode pipeline stages. */
	virtual unsigned int get_pipeline_stages(void) = 0;

	/** Set the number of IExecute pipeline stages.
	 * @param s Number of IExecute pipeline stages. */
	void
	set_iexec_pipeline_stages(unsigned int s)
	{
		if (s == 0)
			throw invalid_argument("Must have at least one IExec "
					"pipeline stage.");

		iexec_pipeline_stages = s;
	}

protected:
	/** Default constructor. */
	IDecode()
	: active_warp(0), raw_stalls(0ul), read_bank_conflict_stalls(0ul),
	  resource_busy_stalls(0ul), sidiv_pipe_stall(0),
	  sidiv_issue_dist_stall(0), iexec_pipeline_stages(3),
	  cpop_can_inject(false) {};

	/** Return the currently active warp for an instruction.
	 * @param op Instruction to fetch the currently active warp for.
	 * @return The currently active warp. */
	unsigned int
	getCol(Instruction &op)
	{
		if (opCategory(op.getOp()) == CAT_ARITH_RCPU)
			return active_warp / (FPUS/RCPUS);
		else
			return active_warp;
	}

	/** Return the currently active sub-warp.
	 *
	 * Since RCPUs are provisioned at 1/4th the number of FPUs, warps must
	 * be split up further. For all other instructions, the sub-warp is
	 * always zero
	 * @param op Instruction to derive the currently active warp of.
	 * @return The currently active sub-warp. */
	unsigned int
	getSubcol(Instruction &op)
	{
		if (opCategory(op.getOp()) == CAT_ARITH_RCPU)
			return active_warp % (FPUS/RCPUS);
		else
			return 0;
	}

	/** Add implicit source operand(s).
	 * @param op Instruction to update. */
	void
	op_add_implicit_src(Instruction &op)
	{
		switch (op.getOp()) {
		case OP_LDGLIN:
		case OP_STGLIN:
		case OP_LDSPLIN:
		case OP_STSPLIN:
		case OP_SLDSP:
		case OP_LDG2SPTILE:
		case OP_STG2SPTILE:
			if (op.getSrcs() < 2)
				op.addSrc(Operand(0));
			if (op.getSrcs() < 3)
				op.addSrc(Operand(0));
			break;
		case OP_SLDG:
			if (op.getSrcs() < 2)
				op.addSrc(Operand(1));
			break;
		case OP_EXIT:
			if (op.getSrcs() == 0)
				op.addSrc(Operand(REGISTER_VSP, VSP_ONE));
			break;
		case OP_CALL:
			if (op.getSrcs() == 1)
				op.addSrc(Operand(REGISTER_VSP, VSP_ONE));
			break;
		case OP_CPUSH:
			if (op.getSrcs() >= 2)
				break;

			switch (op.getSubOp().cpush) {
			case CPUSH_IF:
				op.addSrc(Operand(REGISTER_VSP, VSP_CTRL_RUN));
				break;
			case CPUSH_BRK:
				op.addSrc(Operand(REGISTER_VSP, VSP_CTRL_BREAK));
				break;
			case CPUSH_RET:
				op.addSrc(Operand(REGISTER_VSP, VSP_CTRL_RET));
				break;
			default:
				assert(false);
				break;
			}
			break;
		case OP_DBG_PRINTCMASK:
			if (op.getSrcs())
				break;

			switch (op.getSubOp().printcmask) {
			case PRINTCMASK_IF:
				op.addSrc(Operand(REGISTER_VSP, VSP_CTRL_RUN));
				break;
			case PRINTCMASK_BRK:
				op.addSrc(Operand(REGISTER_VSP, VSP_CTRL_BREAK));
				break;
			case PRINTCMASK_RET:
				op.addSrc(Operand(REGISTER_VSP, VSP_CTRL_RET));
				break;
			case PRINTCMASK_EXIT:
				op.addSrc(Operand(REGISTER_VSP, VSP_CTRL_EXIT));
				break;
			default:
				assert(false);
				break;
			}
			break;
			break;
		default:
			break;
		}
	}

	/** Add destinations explicitly and manage commit bit for operation.
	 * @param op Operation to update. */
	void
	op_process_implicit_dst(Instruction &op)
	{
		if (opCategory(op.getOp()) == CAT_ARITH_RCPU)
			op.setCommit(getSubcol(op) == (FPUS/RCPUS) - 1);

		switch (op.getOp()) {
		case OP_CPUSH:
			op.setCommit(active_warp == last_warp);
			break;
		case OP_BRA:
		case OP_CMASK:
			op.setDst(Operand(REGISTER_VSP,VSP_CTRL_RUN));
			break;
		case OP_BRK:
			op.setDst(Operand(REGISTER_VSP,VSP_CTRL_BREAK));
			break;
		case OP_EXIT:
			op.setDst(Operand(REGISTER_VSP,VSP_CTRL_EXIT));
			break;
		case OP_CALL:
		case OP_RET:
			op.setDst(Operand(REGISTER_VSP,VSP_CTRL_RET));
			break;
		case OP_LDGBIDX:
		case OP_STGBIDX:
		case OP_LDGCIDX:
		case OP_STGCIDX:
		case OP_LDSPBIDX:
		case OP_STSPBIDX:
			op.setDst(Operand(REGISTER_VSP,VSP_MEM_DATA));
			break;
		case OP_SENTINEL:
			/* Invalid instruction */
			op.kill();
			break;
		default:
			break;
		}

		if (op.writesCMASK())
			op.setCommit(active_warp == last_warp);
	}

	/** Request ldst buffer translation.
	 * @param op Instruction with potential ldst request. */
	void
	op_ldst_xlat_idx(Instruction &op)
	{
		if (op.isDead() || (!op.ldst() && op.getOp() != OP_BUFQUERY))
			return;

		switch (op.getOp()) {
		case OP_LDSPLIN:
		case OP_STSPLIN:
		case OP_LDSPBIDX:
		case OP_STSPBIDX:
		case OP_SLDSP:
			out_sp_xlat_idx.write(op.getSrc(0).getValue());
			break;
		case OP_LDG2SPTILE:
		case OP_STG2SPTILE:
			out_sp_xlat_idx.write(op.getDst().getValue());
			/* fall-through */
		case OP_LDGLIN:
		case OP_STGLIN:
		case OP_LDGBIDX:
		case OP_STGBIDX:
		case OP_LDGCIDX:
		case OP_STGCIDX:
		case OP_LDGIDXIT:
		case OP_STGIDXIT:
		case OP_SLDG:
		case OP_BUFQUERY:
			out_xlat_idx.write(op.getSrc(0).getValue());
			break;
		default:
			cout << "ERROR: ldst instruction without supported "
					"buffer idx translation" << endl;
			break;
		}
	}

	/** Prepare a single read request in the req struct for RegFile.
	 *
	 * @param i   Index of source register in op and req.
	 * @param req Request to be pushed out to the register file at the
	 * 	      end of this cycle.
	 * @param op  Instruction associated with this request.
	 * @param col Currently active column. 0 for scalar instructions.
	 * @param wg  Active work-group slot. */
	void
	forward_read_req(unsigned int i, reg_read_req<THREADS/FPUS> &req,
			Instruction &op, unsigned int col, sc_uint<1> wg)
	{
		/* Only issue this read operation once for all subcolumns. */
		if (opCategory(op.getOp()) == CAT_ARITH_RCPU && getSubcol(op) != 0)
			return;

		switch (op.getOp()) {
		case OP_DBG_PRINTVGPR:
			if (i == 0)
				col = (op.getSrc(1).getValue() >> const_log2(FPUS));
			break;
		default:
			break;
		}

		req.r[i] = Log_1;
		req.reg[i] = op.getSrc(i).getRegister<THREADS/FPUS>(wg, col);

		if (req.reg[i].type == REGISTER_IMM)
			req.imm[i] = op.getSrc(i).getValue();
	}

	/** Enqueue write request to scoreboard if applicable.
	 * @param op The instruction to decode the write operation from. */
	void
	sb_write_req(Instruction &op)
	{
		Register<THREADS/FPUS> reg;

		if (op.isDead() || op.ldst()) {
			out_enqueue_sb.write(false);
			out_enqueue_sb_cstack_write.write(false);
			return;
		}

		/* Control stack is a special case, because they cannot be
		 * encoded in the destination of the op - on account of BRA and
		 * CALL already having a VSP register as a destination. */
		if (op.doesCPUSH() && op.getCommit() && !op.getOnCStackSb()) {
			out_enqueue_sb_cstack_write.write(true);
			out_enqueue_sb_cstack_wg.write(in_wg.read());
			op.setOnCStackSb(true);
		} else {
			out_enqueue_sb_cstack_write.write(false);
		}

		if (op.hasDst() &&
		     (opCategory(op.getOp()) != CAT_ARITH_RCPU || op.getCommit())) {
			out_enqueue_sb.write(!op.getOnSb());
			op.setOnSb(true);
			reg = op.getDst().getRegister<THREADS/FPUS>(in_wg.read(), getCol(op));
			out_req_w_sb.write(reg);
		} else {
			out_enqueue_sb.write(false);
		}
	}

	/** Pipeline invalidation hook. */
	virtual void pipe_invalidate(void) {}

	/** Select the next operation to bring into the IDecode pipeline.
	 *
	 * There is a subtlety in the decoder. During normal
	 * operation we decode the op from in_insn (IMem) and
	 * enumerate its warps if necessary.
	 * When a signal comes in stating no thread is active,
	 * we drop whatever we are doing and enumerate a CPOP
	 * instruction, to get the top entry off the stack.
	 * If the stack is empty we should exit.
	 *
	 * CPOP itself may not be interrupted by the
	 * in_thread_active signal, as we are modifying
	 * the mask itself.
	 *
	 * @todo If IExecute still has a CPOP in-flight, we
	 * should not consider the no-thread-active signal yet.
	 * Scoreboard isn't going to help, because we don't know
	 * which exact register will be written to until the
	 * data is fetched from the control stack. How to filter
	 * invalid CPOP-injections?
	 * @param op Reference to the output instruction operation. Contains the
	 * 	     op from the previous cycle upon calling.
	 * @param pc Reference to current program counter, will be overwritten
	 * 	     upon assigning a new instruction from IMem.
	 */
	void
	select_op(Instruction &op, sc_uint<PC_WIDTH> &pc)
	{
		if (in_wg_finished.read()[in_wg.read()]) {
			/* Prevent an injected CPOP. */
			op = Instruction(NOP,{});
			active_warp = 0;
			last_warp = 0;
		} else if (in_pipe_flush.read()) {
			if (debug_output[DEBUG_COMPUTE_TRACE])
				cout << "*** FLUSH IDEC ***" << endl;

			pipe_invalidate();

			/** Uninterruptible doesn't matter when control
			 * flow would have prevented this instruction
			 * from executing in the first place. */
			if (!op.isInjected() || active_warp == 0) {
				active_warp = 0;
				last_warp = 0;
				out_stall_f.write(false);
				out_enqueue_sb.write(false);
				out_enqueue_sb_cstack_write.write(false);
				op = Instruction(NOP,{});
				op.kill();

				cpop_can_inject = true;
			} /* Else continue issuing that CPOP. */
		} else if (!in_thread_active.read()[in_wg.read()] &&
			   !out_stall_f.read()) {
			if (cpop_can_inject) {
				op = Instruction(OP_CPOP,{});
				op.inject();
				last_warp = in_last_warp[in_wg.read()].read();
				active_warp = 0;
				cpop_can_inject = false;
			} else if (active_warp == 0){
				/* We already had a CPOP. Wait for the
				 * in_pipe_flush before issuing the
				 * next */
				op = Instruction(NOP,{});
				active_warp = 0;
				last_warp = 0;
			}
		} else if (active_warp == 0 &&
			   !out_stall_f.read()) {
			op = in_insn.read();
			pc = in_pc.read();

			if (op.isVectorInstruction())
				last_warp = in_last_warp[in_wg.read()].read();
			else
				last_warp = 0;

			if (opCategory(op.getOp()) == CAT_ARITH_RCPU)
				last_warp = ((last_warp + 1) * (FPUS/RCPUS)) - 1;
			op_add_implicit_src(op);
		} /* Else we continue executing the current op */
	}

	/** Find the first conflict provided the conflict bitmaps
	 * @param raw Read-After-Write conflict map.
	 * @param conflicts Bank conflict bitmap.
	 * @return The first index indicating a conflict, -1 if no conflict
	 * found. */
	int
	first_conflict(sc_bv<3> raw, sc_bv<3> conflicts) const
	{
		sc_bv<3> retry;
		unsigned int i;

		retry = raw | conflicts;

		for (i = 0; i < 3; i++) {
			if (retry[i] == Log_1)
				return i;
		}

		return -1;
	}

	/** Debugging print for stall operations.
	 * @param lc First conflict index.
	 * @param op Instruction associated with this conflicts.
	 * @param reason String describing the source of the conflict. */
	void
	debug_print_stall(unsigned int lc, Instruction &op,
			const string reason) const
	{
		cout << sc_time_stamp() << "STALL(" << reason << ") Operand "
				<< lc+1 << " in " << op << endl;
	}

	/** Start the idiv stall counters.
	 * The 8-cycle latency is derived from Intels Radix-16 division
	 * implementation, a cheap DDR SRT divider that should meet 1GHz. */
	void
	set_sidiv_stall_counters(void)
	{
		sidiv_issue_dist_stall = 8;
		sidiv_pipe_stall = max(8 - (int)(iexec_pipeline_stages), 0);
	}

	/** Decrement the sidiv counters. */
	void
	decrement_sidiv_stall_counters(void)
	{
		sidiv_issue_dist_stall = max(sidiv_issue_dist_stall - 1, 0);
		sidiv_pipe_stall = max(sidiv_pipe_stall - 1, 0);
	}

	/** Return true iff the next instruction can be issued wrt. SIDIV
	 * counters.
	 * @param op Instruction waiting to be issued.
	 * @param wg Active work-group slot.
	 * @return true iff this instruction can advance from IDecode to
	 * 	   IExecute. */
	bool
	op_can_issue(Instruction &op, sc_uint<1> wg)
	{
		if (op.getOp() == OP_CPOP && !op.isDead() && in_sb_cpop_stall[wg].read())
			return false;
		else if (op.getOp() == OP_SIDIV || op.getOp() == OP_SIMOD)
			return sidiv_issue_dist_stall == 0;
		else
			return sidiv_pipe_stall == 0;
	}
};

}

#endif /* COMPUTE_CONTROL_IDECODE_H */
