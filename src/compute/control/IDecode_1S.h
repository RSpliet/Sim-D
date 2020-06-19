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

#ifndef COMPUTE_CONTROL_IDECODE_1S_H
#define COMPUTE_CONTROL_IDECODE_1S_H

#include "compute/control/IDecode.h"
#include "util/constmath.h"

using namespace std;
using namespace sc_dt;
using namespace sc_core;
using namespace compute_model;
using namespace simd_model;
using namespace isa_model;

namespace compute_control {

/** Instruction decode pipeline, single stage, fetch all 3 operands of the
 * same instruction.
 *
 * This decoder will never stall when paired with a 3R1W register file.
 * Unlikely to be feasible to implement, but represents the "perfect register
 * file" case.
 * @param PC_WIDTH Number of bits in a PC.
 * @param THREADS Total number of threads in a workgroup.
 * @param LANES Number of vector lanes in each SIMD cluster.
 */
template <unsigned int PC_WIDTH, unsigned int THREADS = 1024,
		unsigned int FPUS = 128, unsigned int RCPUS = 32,
		unsigned int XLAT_ENTRIES = 32>
class IDecode_1S : public IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>
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
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::in_last_warp;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::in_thread_active;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::in_wg_finished;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_pc;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_insn;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_req;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_req_sb;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_ssp_match;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_enqueue_sb;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_req_w_sb;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_wg;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_col_w;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_subcol_w;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_stall_f;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::in_raw;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::in_req_conflicts;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::in_pipe_flush;
	using IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>::out_xlat_idx;

	/** Construct thread. */
	SC_CTOR(IDecode_1S)
	: IDecode<PC_WIDTH,THREADS,FPUS,RCPUS,XLAT_ENTRIES>()
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();
	}

	unsigned int
	get_pipeline_stages(void)
	{
		return 1;
	}

private:
	/** Prepare the read request struct for RegFile.
	 * @param req 	    Request to be pushed out to the register file at the
	 * 		    end of this cycle.
	 * @param conflicts Mask of conflicts in the previous cycle, all-1 if
	 * 		    no conflict occurred.
	 * @param op	    Instruction associated with this request.
	 * @param col	    Currently active column. */
	void
	forward_read_reqs(reg_read_req<THREADS/FPUS> &req, sc_bv<3> &conflicts,
			Instruction &op, unsigned int col)
	{
		unsigned int i;
		ISAOp opcode;

		req.r = 0;
		opcode = op.getOp();

		for (i = 0; i < op.getSrcs(); i++) {
			if (op.isDead() || !conflicts[i])
				continue;

			forward_read_req(i, req, op, col, in_wg.read());
		}
	}

	/** Main thread. */
	void
	thread_lt(void)
	{
		Instruction op;
		sc_uint<PC_WIDTH> pc;
		sc_bv<3> r;
		reg_read_req<THREADS/FPUS> req;
		sc_bv<3> raw;
		sc_bv<3> conflicts;
		sc_bv<3> op_retry;
		int fc;
		bool iexec_resource_free;

		op_retry = 7;
		pc = 0;

		while (true) {
			wait();

			select_op(op, pc);

			forward_read_reqs(req, op_retry, op, getCol(op));

			if (debug_output[DEBUG_COMPUTE_TRACE])
				cout << sc_time_stamp() << " IDecode: " <<
					pc << " " << getCol(op) << "." <<
					getSubcol(op) << " " << op << endl << endl;

			/* Add implicit destination operands. */
			/** @todo On "vector" (conditional...) branches like
			 * bra, call, cpop, we might be able to resolve pipeline
			 * bubbles for workgroups with multiple warps by issuing
			 * the PC earlier. Figure out constraints and timing. */
			op_process_implicit_dst(op);
			op_ldst_xlat_idx(op);

			out_pc.write(pc);
			out_wg.write(in_wg.read());
			out_col_w.write(getCol(op));
			out_subcol_w.write(getSubcol(op));
			out_req.write(req);
			out_req_sb.write(req);
			out_ssp_match.write(op.blockOnSSPWrites());

			conflicts = in_req_conflicts.read();
			raw = in_raw.read();
			op_retry = conflicts | raw;

			iexec_resource_free = op_can_issue(op, in_wg.read());
			decrement_sidiv_stall_counters();

			/* If the scoreboard directs us to stall, do it.
			 * Otherwise, update active warp and stall bit. */
			if (op_retry.or_reduce() || !iexec_resource_free) {
				out_insn.write(Instruction(NOP,{}));
				out_stall_f.write(true);
				out_enqueue_sb.write(false);

				if (raw.or_reduce())
					raw_stalls++;
				else if (conflicts.or_reduce())
					read_bank_conflict_stalls++;
				else if (!iexec_resource_free)
					resource_busy_stalls++;

				if (debug_output[DEBUG_COMPUTE_STALLS]) {
					fc = first_conflict(raw, conflicts);
					debug_print_stall(fc, op, raw[fc] ?
						"RAW" : (conflicts[fc] ?
						"Bank conflict" : "Resource unavailable"));
				}
			} else {
				sb_write_req(op);
				out_insn.write(op);
				if (op.getOp() == OP_SIDIV ||
				    op.getOp() == OP_SIMOD)
					set_sidiv_stall_counters();
				op_retry = 7;

				if (active_warp == last_warp) {
					out_stall_f.write(false);
					active_warp = 0;
				} else {
					out_stall_f.write(true);
					active_warp++;
					op.setOnSb(false);
				}
			}
		}
	}
};

}

#endif /* COMPUTE_CONTROL_IDECODE_1S_H */
