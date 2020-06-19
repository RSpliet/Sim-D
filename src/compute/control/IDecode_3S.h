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

#ifndef COMPUTE_CONTROL_IDECODE_3S_H
#define COMPUTE_CONTROL_IDECODE_3S_H

#include "compute/control/IDecode.h"

using namespace std;
using namespace sc_dt;
using namespace sc_core;
using namespace compute_model;
using namespace simd_model;
using namespace isa_model;

namespace compute_control {

/** IDecode pipeline stage, containing all output signals for IDecode.
 * @param PC_WIDTH Number of bits for the PC.
 * @param THREADS Number of threads in a work-group.
 * @param FPUS number of FPUS per SimdCluster.
 * @param RCPUS Number of floating point ReCiProcal units per SimdCluster.
 */
template <unsigned int PC_WIDTH, unsigned int THREADS = 1024,
		unsigned int FPUS = 128, unsigned int RCPUS = 32>
class IDecode_pipe
{
public:
	Instruction insn; /**< Instruction. */
	sc_uint<1> wg; /**< Associated work-group. */
	sc_uint<PC_WIDTH> pc; /**< Program counter for this instruction. */
	sc_uint<const_log2(THREADS/FPUS)> col_w; /**< Column for write-back. */
	sc_uint<const_log2(FPUS/RCPUS)> subcol_w; /**< Sub-column for write-back */
	sc_bv<32> req_sb_pop; /**< Population of scoreboard to test against. */

	/** Default constructor */
	IDecode_pipe()
	{
		reset();
	}

	/** New entry constructor. */
	IDecode_pipe(Instruction i, sc_uint<1> w, sc_uint<PC_WIDTH> p,
			sc_uint<const_log2(THREADS/FPUS)> c,
			sc_uint<const_log2(FPUS/RCPUS)> sc)
	: insn(i), wg(w), pc(p), col_w(c), subcol_w(sc)
	{
		/** The reason why this assignment is correct is veeeeery
		 * subtle. One cycle after this assignment you read back the
		 * mask that *excludes* the reg written by this instruction and
		 * includes the entry written by the previous cycle. This mask
		 * lags behind the state of the scoreboard by one cycle. Hence,
		 * anding req_sb_pop with the mask presented on the next cycle
		 * will give us the mask we actually want.
		 */
		req_sb_pop = 0;
		req_sb_pop.b_not();
	}

	/** Perform a reset of this pipeline stage. */
	void
	reset(void)
	{
		insn = Instruction(NOP,{});
		insn.kill();
		pc = 0;
		col_w = 0;
		subcol_w = 0;
		req_sb_pop = 0;
	}

	/** Return true iff the instruction in this pipeline stage is empty. */
	bool
	isEmpty(void)
	{
		return insn.isDead();
	}

	/** Print output stream operation.
	 * @param os Output stream
	 * @param p IDecode pipeline stage to print
	 * @return Output stream. */
	inline friend ostream &
	operator<<(ostream &os, IDecode_pipe<PC_WIDTH,THREADS,FPUS,RCPUS> const &p)
	{
		os << "wg(" << p.wg << ") " << p.pc << ": " << p.insn;
		return os;
	}
};

/** Instruction decode pipeline, three-stage.
 *
 * Fetches one operand per cycle for each instruction in the pipeline.
 * @param PC_WIDTH Number of bits in a PC.
 * @param THREADS Total number of threads in a workgroup.
 * @param LANES Number of vector lanes in each SIMD cluster.
 * @param XLAT_ENTRIES Number of supported mapped buffers.
 */
template <unsigned int PC_WIDTH, unsigned int THREADS = 1024,
		unsigned int FPUS = 128, unsigned int RCPUS = 32,
		unsigned int XLAT_ENTRIES = 32>
class IDecode_3S : public IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>
{
protected:
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::active_warp;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::raw_stalls;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::read_bank_conflict_stalls;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::resource_busy_stalls;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::last_warp;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::getCol;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::getSubcol;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::op_add_implicit_src;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::op_process_implicit_dst;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::forward_read_req;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::sb_write_req;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::op_ldst_xlat_idx;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::in_entries_pop;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::select_op;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::first_conflict;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::debug_print_stall;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::set_sidiv_stall_counters;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::decrement_sidiv_stall_counters;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::op_can_issue;
	using sc_module::sensitive;
	using sc_module::wait;

public:
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::in_clk;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::in_insn;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::in_pc;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::in_wg;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::in_wg_width;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::in_last_warp;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::in_thread_active;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::in_wg_finished;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_pc;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_insn;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_req;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_req_sb;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_ssp_match;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_enqueue_sb;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_enqueue_sb_cstack_write;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_req_w_sb;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_wg;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_col_w;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_subcol_w;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_stall_f;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::in_raw;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::in_req_conflicts;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::in_pipe_flush;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_xlat_idx;

	/** Incoming operands from register file. */
	sc_in<sc_uint<32> > in_operand[2][FPUS];

	/** First two outgoing operands to IExecute. The third will come
	 * straight from the register file. */
	sc_inout<sc_uint<32> > out_operand[2][FPUS];

	/** Population for each read request. */
	sc_inout<sc_bv<32> > out_req_sb_pop[3];

	/** Retry read/write on next cycle. */
	sc_bv<3> op_retry;

	/** Construct thread. */
	SC_CTOR(IDecode_3S)
	: IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>(), op_retry(7)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();
	}

	unsigned int
	get_pipeline_stages(void)
	{
		return 3;
	}

private:
	/** Pipeline stages. */
	IDecode_pipe<PC_WIDTH,THREADS,FPUS,RCPUS> pipe[3];

	/** Operand fetched in stage 1, stored in stage 2. */
	sc_uint<32> operand_0[FPUS];

	/** Prepare the read request struct for RegFile.
	 * @param req Reference to set of read requests.
	 * @param read_mask Reference to the active read mask. Permits omitting
	 * 		    reads that were already serviced in a previous
	 * 		    cycle, but didn't advance in the pipeline due to
	 * 		    stalls. */
	void
	forward_read_reqs(reg_read_req<THREADS/FPUS> &req, sc_bv<3> &read_mask)
	{
		unsigned int i;
		sc_uint<const_log2(THREADS/FPUS)> col;

		req.r = 0;

		for (i = 0; i < 3; i++) {
			if (!read_mask[i] ||
			    pipe[i].insn.isDead() ||
			    pipe[i].insn.getSrcs() <= i)
				continue;

			col = pipe[i].col_w;

			forward_read_req(i, req, pipe[i].insn, col, pipe[i].wg);
		}
	}

	/** Kill instructions for all pipeline entries.
	 *
	 * Retain the instruction itself, to clear out the scoreboard.
	 */
	void
	pipe_invalidate(void)
	{
		unsigned int i;

		for (i = 0; i < 3; i++) {
			if (!pipe[i].insn.isInjected())
				pipe[i].insn.kill();
		}

		op_retry = 7;
	}

	/** Kill instructions for all pipeline entries corresponding with wg.
	 *
	 * Retain the instruction itself, to clear out the scoreboard.
	 * @param wg Workgroup for which the pipeline must be invalidated.
	 */
	void
	pipe_invalidate(sc_uint<1> wg)
	{
		unsigned int i;

		for (i = 0; i < 3; i++) {
			if (pipe[i].wg == wg)
				pipe[i].insn.kill();
		}
	}

	/** Main thread. */
	void
	thread_lt(void)
	{
		Instruction op;
		sc_uint<PC_WIDTH> pc;
		reg_read_req<THREADS/FPUS> req;
		sc_bv<3> raw;
		sc_bv<3> conflicts;
		sc_bv<32> entries_pop[2];
		sc_bv<32> entries_pop_all;
		unsigned int l;
		unsigned int i;
		int fc;
		bool iexec_resource_free;

		entries_pop_all = 0;
		entries_pop_all.b_not();

		while (true) {
			wait();

			/* Idle. Once a WG finishes, the pipeline will simply
			 * be full of rubbish for that WG. */
			if (in_wg_finished.read()[0])
				pipe_invalidate(0);
			if (in_wg_finished.read()[1])
				pipe_invalidate(1);

			/* Determine OP */
			select_op(op, pc);

			/* Update SB entry population registers in pipeline */
			entries_pop[0] = in_entries_pop[0].read();
			entries_pop[1] = in_entries_pop[1].read();
			pipe[0].req_sb_pop &= entries_pop[pipe[0].wg.to_uint()];
			pipe[1].req_sb_pop &= entries_pop[pipe[1].wg.to_uint()];
			pipe[2].req_sb_pop &= entries_pop[pipe[2].wg.to_uint()];

			/* Pipeline progression */
			if (pipe[2].isEmpty() && !op_retry[1]) {
				for (l = 0; l < FPUS; l++) {
					out_operand[0][l].write(operand_0[l]);
					out_operand[1][l].write(in_operand[1][l].read());
				}

				pipe[2] = pipe[1];
				pipe[1].reset();

				out_wg.write(pipe[2].wg);
				out_col_w.write(pipe[2].col_w);
				out_subcol_w.write(pipe[2].subcol_w);
				out_pc.write(pipe[2].pc);
				op_ldst_xlat_idx(pipe[2].insn);

				op_retry[2] = Log_1;
			}

			if (pipe[1].isEmpty() && !op_retry[0]) {
				pipe[1] = pipe[0];

				/* XXX: criteria? */
				for (l = 0; l < FPUS; l++) {
					operand_0[l] = in_operand[0][l].read();
				}

				pipe[0].reset();
				op_retry[1] = Log_1;
			}

			if (op.isDead()) {
				out_enqueue_sb.write(false);
				out_enqueue_sb_cstack_write.write(false);
			} else if (pipe[0].isEmpty()) {
				/* We need a new instruction */
				pipe[0] = IDecode_pipe<PC_WIDTH,THREADS,FPUS,RCPUS>(
					op, in_wg.read(), pc, getCol(op), getSubcol(op));
				op_retry[0] = Log_1;

				/** @todo On "vector" (conditional...) branches
				 * like bra, call, cpop, we might be able to
				 * resolve pipeline bubbles for workgroups with
				 * multiple warps by issuing the PC earlier.
				 * Figure out constraints and timing. */
				op_process_implicit_dst(pipe[0].insn);

				if (!pipe[0].insn.getOnSb())
					sb_write_req(pipe[0].insn);
				else
					out_enqueue_sb.write(false);

				if (active_warp == last_warp) {
					out_stall_f.write(false);
					active_warp = 0;
				} else {
					out_stall_f.write(true);
					active_warp++;
				}
			} else {
				/* Performance counters. Some conflicts could
				 * instead fill up existing bubbles, in which
				 * case they are not resulting in a stall.
				 * Hence only count if a stall leads us to not
				 * pull a new instruction into the pipeline. */
				if (raw.or_reduce())
					raw_stalls++;
				else if (conflicts.or_reduce())
					read_bank_conflict_stalls++;
				else if (!iexec_resource_free)
					resource_busy_stalls++;

				out_stall_f.write(true);
				out_enqueue_sb.write(false);
			}

			forward_read_reqs(req, op_retry);

			for (i = 0; i < 3; i++)
				out_req_sb_pop[i].write(pipe[i].req_sb_pop);

			if (debug_output[DEBUG_COMPUTE_TRACE]) {
				cout << sc_time_stamp() << " IDecode[0]: " <<
					pipe[0] << " REQ_SB_POP: " <<
					pipe[0].req_sb_pop << endl;
				cout << sc_time_stamp() << " IDecode[1]: " <<
					pipe[1] << " REQ_SB_POP: " <<
					pipe[1].req_sb_pop << endl;
				cout << sc_time_stamp() << " IDecode[2]: " <<
					pipe[2] << " REQ_SB_POP: " <<
					pipe[2].req_sb_pop << endl;
			}

			out_req.write(req);
			out_req_sb.write(req);
			out_ssp_match.write(pipe[0].insn.blockOnSSPWrites());

			conflicts = in_req_conflicts.read();
			raw = in_raw.read();
			op_retry = conflicts | raw;

			if (debug_output[DEBUG_COMPUTE_STALLS] &&
					op_retry.or_reduce()) {
				fc = first_conflict(raw, conflicts);
				debug_print_stall(fc, pipe[fc].insn,
					raw[fc] ? "RAW" : "RF Bank");
			}

			/** Issue instruction if permitted */
			iexec_resource_free = op_can_issue(pipe[2].insn,
					pipe[2].wg);
			decrement_sidiv_stall_counters();

			if (!op_retry[2] && iexec_resource_free) {
				if (pipe[2].insn.getOp() == OP_SIDIV ||
				    pipe[2].insn.getOp() == OP_SIMOD)
					set_sidiv_stall_counters();

				out_insn.write(pipe[2].insn);
				pipe[2].reset(); /* Mark as empty. */
			} else {
				out_insn.write(Instruction(NOP,{}));
			}
		}
	}
};

}

#endif /* COMPUTE_CONTROL_IDECODE_3S_H */
