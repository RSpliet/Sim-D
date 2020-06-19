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

#include <deque>

#include "isa/analysis/CycleSim.h"
#include "util/defaults.h"
#include "util/debug_output.h"

using namespace compute_control;
using namespace isa_model;
using namespace std;

namespace isa_analysis {

/** Data structure to hold the current pipeline state.
 *
 * @todo See how much of the static functions below might be a better fit for
 * object methods. */
class CSContext {
public:
	unsigned long cycle; /**< Global cycle counter. */
	unsigned int cycle_bb; /**< Currently processed BB. */
	unsigned long cycle_bb_start; /**< First cycle of this BB exec. */
	unsigned long cycle_bb_last; /**< Last cycle of observed exec for
						this BB. */

	unsigned int sidiv_iexec_block; /**< Cycle counter to block IExec
						when sidiv/simod is pending */
	int sidiv_issue_dist; /**< Cycle counter guarding issue distance
					between two sidiv/simod instructions.*/
	unsigned int cstack_wr_pending; /**< Number of CSTACK writes in
						the pipeline. */

	unsigned int pipe_dec_depth; /**< Pipeline depth of decode phase. */
	Instruction *pipe_dec = nullptr; /**< Pipeline for decode. */
	unsigned int *pipe_dec_col = nullptr; /**< Column for decode vec r/w */
	unsigned int pipe_exec_depth; /**< Pipeline depth of execute phase. */
	Instruction *pipe_exec = nullptr; /**< Pipeline for execute insns.*/

	/** True iff this pipeline context is for a warm pipeline */
	bool warm;

	/** Scoreboard queue */
	deque<Register<COMPUTE_THREADS/COMPUTE_FPUS> > sb;

	/** Function pointer to simulate an IDecode cycle, adding an
	 * instruction to the pipeline.
	 * @param ctx Cycle Simulator context.
	 * @param op Instruction to try to add to the pipeline */
	bool (*pipeIDecCycle)(CSContext &ctx, Instruction *op, unsigned int);

	/** Constructor,
	 * @param idec_impl Specifies whether this is a 1-stage or 3-stage
	 * 	 	    instruction decoder.
	 * @param exec_depth Pipeline depth of the execute step.
	 * @param warm True iff this context is a warm-pipeline context. */
	CSContext(IDecode_impl idec_impl, unsigned int exec_depth, bool warm);

	/** Destructor. */
	~CSContext(void)
	{
		delete[] pipe_exec;
		delete[] pipe_dec;
		delete[] pipe_dec_col;

		pipe_exec = nullptr;
		pipe_dec = nullptr;
		pipe_dec_col = nullptr;
	}
};

static void
edgePenalties(CSContext &ctx, Program &p)
{
	vector<BB *>::const_iterator bbit;
	BB *bb;
	list<CFGEdge *>::const_iterator edgeit;
	CFGEdge *edge;
	BB *sinkbb;
	unsigned int pipe_depth;
	unsigned int pd;
	unsigned int penalty;

	pipe_depth = ctx.pipe_dec_depth + ctx.pipe_exec_depth;

	for (bbit = p.cbegin(); bbit != p.cend(); bbit++) {
		bb = *bbit;
		for (edgeit = bb->cfg_out_begin(); edgeit != bb->cfg_out_end(); edgeit++) {
			edge = *(edgeit);
			sinkbb = edge->getDst();

			/** If the first instruction of a BB is a scalar int div
			 * or mod, the cold pipeline warm-up could be a few
			 * cycles extra. Compensate. */
			pd = pipe_depth;
			if (sinkbb->begin() != sinkbb->end() &&
			     ((*sinkbb->begin())->getOp() == OP_SIDIV ||
			      (*sinkbb->begin())->getOp() == OP_SIMOD)) {
				pd = std::max(pd, ctx.pipe_dec_depth + 8);
			}

			switch(edge->getType()) {
			case EDGE_FALLTHROUGH:
				edge->setCycles(bb->getPipelinePenalty());
				break;
			case EDGE_CTRLFLOW:
				sinkbb = edge->getDst();
				edge->setCycles(pd);
				break;
			case EDGE_CPOP_INJECTED:
				/* XXX: Double-check this penalty. */
				penalty = (edge->CPOPCount() - 1) *
					(pipe_depth + (COMPUTE_THREADS/COMPUTE_FPUS) + 1);
				penalty += (pd + (COMPUTE_THREADS/COMPUTE_FPUS) + 1);
				edge->setCycles(penalty);
				break;
			default:
				cerr << "Unknown CFGEdge type, cannot calculate WCET "
					"for BB transition." << endl;
				break;
			}
		}
	}
}

static void
updateCycleCount(CSContext &ctx, Program &prg, Instruction &op)
{
	BB *bb;

	if (op.getBB() != ctx.cycle_bb) {
		/* XXX: Do we need a dummy start node to capture pipeline warmup
		 * overhead? */
		if (ctx.cycle_bb >= 0) {
			bb = prg.getBB(ctx.cycle_bb);
			if (bb)
				bb->setExecCycles(ctx.cycle - (ctx.cycle_bb_start - 1), ctx.warm);
		}
		ctx.cycle_bb = op.getBB();
		ctx.cycle_bb_start = ctx.cycle;
	}

	ctx.cycle_bb_last = ctx.cycle;
}

static Instruction
pipeExecCycle(CSContext &ctx, Program &prg)
{
	unsigned int i;
	Instruction ret = ctx.pipe_exec[ctx.pipe_exec_depth-1];

	/* Stages 1-n always progress */
	for (i = ctx.pipe_exec_depth - 1; i > 1; i--) {
		ctx.pipe_exec[i] = ctx.pipe_exec[i-1];
	}

	/* Div might hold up the pipeline for a bit to guarantee a correct
	 * write-back order in the light of it being radix-16, 8-cycles
	 * non-pipelined. */
	if ((ctx.pipe_exec[0].getOp() == OP_SIDIV ||
	     ctx.pipe_exec[0].getOp() == OP_SIMOD) &&
	    ctx.sidiv_iexec_block) {
		ctx.sidiv_iexec_block--;
		ctx.pipe_exec[1] = Instruction();
	} else {
		ctx.pipe_exec[1] = ctx.pipe_exec[0];
		ctx.pipe_exec[0] = Instruction();
	}

	if (ret.getOnSb())
		ctx.sb.pop_front();

	if (ret.getOnCStackSb())
		ctx.cstack_wr_pending--;

	if (!ret.isDead())
		updateCycleCount(ctx, prg, ret);

	return ret;
}

static bool
IDecCanIssue(CSContext &ctx, Instruction &op)
{
	if ((op.getOp() == OP_SIDIV || op.getOp() == OP_SIMOD) &&
		ctx.sidiv_issue_dist > 0)
		return false;

	if (op.getOp() == OP_CPOP && ctx.cstack_wr_pending)
		return false;

	return true;
}

static void
addToScoreboard(CSContext &ctx, Instruction &op, unsigned int column)
{
	Operand dst;
	Register<COMPUTE_THREADS/COMPUTE_FPUS> rt;

	/* Only add the RCPU ops to the scoreboard that commit. */
	if (opCategory(op.getOp()) == CAT_ARITH_RCPU &&
	    !op.getCommit())
		return;

	if (op.hasDst() && opCategory(op.getOp()) != CAT_LDST) {
		dst = op.getDst();

		if (dst.getType() == OPERAND_REG) {
			rt = dst.getRegister<COMPUTE_THREADS/COMPUTE_FPUS>(0, column);
			ctx.sb.push_back(rt);
			op.setOnSb(true);
		}
	}

	if (op.doesCPUSH() &&
	    column == (COMPUTE_THREADS/COMPUTE_FPUS) - 1) {
		ctx.cstack_wr_pending++;
		op.setOnCStackSb(true);
	}
}

/* True iff op's destination is on the SB. */
static bool
regOnScoreboard(CSContext &ctx, Instruction &op, unsigned int src,
		unsigned int column)
{
	if (op.getSrcs() <= src || op.getSrc(src).getType() != OPERAND_REG)
		return false;

	Register<COMPUTE_THREADS/COMPUTE_FPUS> sr =
			op.getSrc(src).getRegister<COMPUTE_THREADS/COMPUTE_FPUS>(0, column);

	for (Register<COMPUTE_THREADS/COMPUTE_FPUS> r : ctx.sb) {\
		if (sr == r)
			return true;
	}

	return false;
}


/* True iff op's destination is on the SB. */
static bool
sspOnScoreboard(CSContext &ctx)
{
	for (Register<COMPUTE_THREADS/COMPUTE_FPUS> r : ctx.sb) {
		if (r.type == REGISTER_SSP)
			return true;
	}

	return false;
}

static bool
pipeIDec1SCycle(CSContext &ctx, Instruction *op, unsigned int column)
{
	unsigned int i;

	ctx.sidiv_issue_dist = max(ctx.sidiv_issue_dist - 1, 0);

	/* Is execute stalled, no free slot? */
	if (ctx.pipe_exec[0].getOp() != OP_SENTINEL)
		return false;

	/* Test constraints for sidiv and CPOP. */
	if(!IDecCanIssue(ctx, ctx.pipe_dec[0]))
		return false;

	/* Scoreboard checks. */
	if (ctx.pipe_dec[0].blockOnSSPWrites() && sspOnScoreboard(ctx))
		return false;

	for (i = 0; i < ctx.pipe_dec[0].getSrcs(); i++) {
		if (regOnScoreboard(ctx, ctx.pipe_dec[0], i,
				ctx.pipe_dec_col[0]))
			return false;
	}

	/* All clear: Advance pipeline. */
	ctx.pipe_exec[0] = ctx.pipe_dec[0];
	addToScoreboard(ctx, ctx.pipe_exec[0], ctx.pipe_dec_col[0]);

	if (ctx.pipe_exec[0].getOp() == OP_SIDIV ||
	    ctx.pipe_exec[0].getOp() == OP_SIMOD) {
		ctx.sidiv_iexec_block = max(8 - int(ctx.pipe_exec_depth), 0);
		ctx.sidiv_issue_dist = 8;
	}

	/* Stick op in the free slot. */
	if (op == nullptr) {
		ctx.pipe_dec[0] = Instruction();
		return true;
	}

	/* Accepted. Let's add it to the pipeline. */
	ctx.pipe_dec[0] = *op;
	ctx.pipe_dec_col[0] = column;

	return true;
}

static void
pipeIDec3SCycleS2(CSContext &ctx)
{
	if (ctx.pipe_exec[0].getOp() != OP_SENTINEL)
		return;

	if (!IDecCanIssue(ctx, ctx.pipe_dec[2]))
		return;

	/* Scoreboard checks. */
	if (ctx.pipe_dec[2].blockOnSSPWrites() && sspOnScoreboard(ctx))
		return;

	if (regOnScoreboard(ctx, ctx.pipe_dec[2], 2, ctx.pipe_dec_col[2]))
		return;

	/* All clear: Advance pipeline. */
	ctx.pipe_exec[0] = ctx.pipe_dec[2];
	ctx.pipe_dec[2] = Instruction();

	/*
	 * Note that this behaviour differs slightly from the SystemC simulation
	 * in that we only add a destination to the scoreboard after it's
	 * cleared all three IDec stages. Instead, we test RAW hazards of the
	 * first two decode stages by looking at the dst of the instructions in
	 * pipe_dec directly. This avoids tracking per instruction test-
	 * masks designed to avoid matching against an instruction's own dest.
	 */
	addToScoreboard(ctx, ctx.pipe_exec[0], ctx.pipe_dec_col[2]);

	if (ctx.pipe_exec[0].getOp() == OP_SIDIV || ctx.pipe_exec[0].getOp() == OP_SIMOD) {
		ctx.sidiv_iexec_block = max(8 - int(ctx.pipe_exec_depth), 0);
		ctx.sidiv_issue_dist = 8;
	}
}

static void
pipeIDec3SCycleS(CSContext &ctx, unsigned int stage)
{
	unsigned int i;
	Register<COMPUTE_THREADS/COMPUTE_FPUS> src, dst;

	if (ctx.pipe_dec[stage+1].getOp() != OP_SENTINEL)
		return;

	if (ctx.pipe_dec[stage].blockOnSSPWrites() && sspOnScoreboard(ctx))
		return;

	if (regOnScoreboard(ctx, ctx.pipe_dec[stage], stage,
			ctx.pipe_dec_col[stage]))
		return;

	if (ctx.pipe_dec[stage].getSrcs() > stage &&
	    ctx.pipe_dec[stage].getSrc(stage).getType() == OPERAND_REG) {
		src = ctx.pipe_dec[stage].getSrc(stage).getRegister<COMPUTE_THREADS/COMPUTE_FPUS>(0, ctx.pipe_dec_col[stage]);

		for (i = stage + 1; i < 3; i++) {
			if (!ctx.pipe_dec[i].hasDst() ||
			    ctx.pipe_dec[i].getDst().getType() != OPERAND_REG)
				continue;

			dst = ctx.pipe_dec[i].getDst().getRegister<COMPUTE_THREADS/COMPUTE_FPUS>(0, ctx.pipe_dec_col[i]);

			if (src == dst)
				return;
		}
	}

	/* All clear: Advance pipeline. */
	ctx.pipe_dec[stage+1] = ctx.pipe_dec[stage];
	ctx.pipe_dec_col[stage+1] = ctx.pipe_dec_col[stage];
	ctx.pipe_dec[stage] = Instruction();
}

static bool
pipeIDec3SCycle(CSContext &ctx, Instruction *op, unsigned int column)
{
	ctx.sidiv_issue_dist = max(ctx.sidiv_issue_dist - 1, 0);

	/* Advance the pipeline one stage at a time. */
	pipeIDec3SCycleS2(ctx);
	pipeIDec3SCycleS(ctx, 1);
	pipeIDec3SCycleS(ctx, 0);

	if (ctx.pipe_dec[0].getOp() != OP_SENTINEL)
		return false;

	/* Stick op in the free slot. */
	if (op == nullptr) {
		ctx.pipe_dec[0] = Instruction();
		return true;
	}

	/* Accepted. Let's add it to the pipeline. */
	ctx.pipe_dec[0] = *op;
	ctx.pipe_dec_col[0] = column;

	return true;
}

static bool
pipeEmpty(CSContext &ctx)
{
	unsigned int i;

	for (i = 0; i < ctx.pipe_dec_depth; i++) {
		if (ctx.pipe_dec[i].getOp() != OP_SENTINEL)
			return false;
	}

	for (i = 0; i < ctx.pipe_exec_depth; i++) {
		if (ctx.pipe_exec[i].getOp() != OP_SENTINEL)
			return false;
	}

	return true;
}

/** Flush out the full contents of the pipeline */
static void
pipeFlush(CSContext &ctx, Program &prg)
{
	Instruction op_sentinel;

	while (!pipeEmpty(ctx)) {
		ctx.cycle++;
		pipeExecCycle(ctx, prg);
		ctx.pipeIDecCycle(ctx, &op_sentinel, 0);
	}
}

/** Process a cycle of simulation.
 *
 * @return True iff the op is added to the pipeline, False if stalled.
 */
static bool
simInstruction(CSContext &ctx, Program &prg, Instruction *op,
		unsigned int column)
{
	unsigned int i;

	/* Skip some cycles for RCPUs that split up into subcolumns. */
	if (op && opCategory(op->getOp()) == CAT_ARITH_RCPU) {
		op->setCommit(false);
		for (i = 1; i < COMPUTE_FPUS/COMPUTE_RCPUS; i++) {
			do {
				ctx.cycle++;
				pipeExecCycle(ctx, prg);
			} while (!ctx.pipeIDecCycle(ctx, op, column));
		}
		op->setCommit(true);
	}

	do {
		ctx.cycle++;
		pipeExecCycle(ctx, prg);
	} while (!ctx.pipeIDecCycle(ctx, op, column));

	return true;
}

/* Defined here such that pipeIDec1SCycle/pipeIDec3SCycle have been defined. */
CSContext::CSContext(IDecode_impl idec_impl, unsigned int exec_depth, bool w)
: cycle(0ul), cycle_bb(-1), cycle_bb_start(0ul), cycle_bb_last(0ul),
  sidiv_iexec_block(0), sidiv_issue_dist(0), cstack_wr_pending(0),
  pipe_exec_depth(exec_depth), warm(w), pipeIDecCycle(nullptr)
{
	switch (idec_impl) {
	case IDECODE_1S:
		pipe_dec_depth = 1;
		pipeIDecCycle = pipeIDec1SCycle;
		break;
	case IDECODE_3S:
		pipe_dec_depth = 3;
		pipeIDecCycle = pipeIDec3SCycle;
		break;
	default:
		cout << "CycleSim: Unknown IDecode implementation." << endl;
		break;
	}

	pipe_dec = new Instruction[pipe_dec_depth];
	pipe_dec_col = new unsigned int[pipe_dec_depth];
	pipe_exec = new Instruction[exec_depth];
}

void
CycleSim(Program &p, IDecode_impl idec_impl, unsigned int iexec_stages)
{
	vector<BB *>::const_iterator bbit;
	list<Instruction *>::iterator opit;
	BB *bb;
	Instruction *op;
	Instruction *nop;
	unsigned int repeat;
	unsigned int i;
	CSContext ctx(idec_impl, iexec_stages, true);

	if (debug_output[DEBUG_WCET_PROGRESS])
		cout << "* Compute pipeline cycle simulation." << endl;

	if (iexec_stages < 3) {
		cerr << "Must have more than three IExecute pipeline stages to "
				"satisfy constraints on RCP/Trigo operations." << endl;
		return;
	}

	nop = new Instruction(NOP);

	/* Iterate over BBs */
	for (bbit = p.cbegin(); bbit != p.cend(); bbit++) {
		CSContext cold_ctx(idec_impl, iexec_stages, false);
		bb = *bbit;

		for (opit = bb->begin(); opit != bb->end(); opit++) {
			op = *opit;

			if (op->isVectorInstruction())
				repeat = COMPUTE_THREADS/COMPUTE_FPUS;
			else
				repeat = 1;

			for (i = 0; i < repeat; i++) {
				simInstruction(cold_ctx, p, op, i);
				simInstruction(ctx, p, op, i);
			}
		}

		op = *(bb->rbegin());
		if (opCategory(op->getOp()) == CAT_LDST)
			pipeFlush(ctx, p);

		nop->setBB(bb->get_id() + 1);
		simInstruction(cold_ctx, p, nop, 0);
		pipeFlush(cold_ctx, p);
	}

	pipeFlush(ctx, p);

	edgePenalties(ctx, p);
}

}
