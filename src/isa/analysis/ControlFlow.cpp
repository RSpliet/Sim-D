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

#include <vector>

#include "isa/analysis/ControlFlow.h"

#include "isa/model/Program.h"
#include "isa/model/Instruction.h"
#include "isa/model/Loop.h"

using namespace isa_model;
using namespace simd_model;
using namespace dram;
using namespace std;

namespace isa_analysis {

/** Control flow analysis state: Current state of stack. */
static vector<pair<unsigned int, BB *> > cstack;
static Loop *cur_loop = nullptr;

static size_t cstack_max_depth;

bool
cstack_equal(vector<pair<unsigned int, BB *> > &a,
		vector<pair<unsigned int, BB *> > &b)
{
	unsigned int i;

	if (a.size() != b.size())
		return false;

	for (i = 0; i < a.size(); i++) {
		if (a[i].first != b[i].first ||
		    a[i].second != b[i].second)
			return false;
	}

	return true;
}

bool
cfg_add_out_cstack(BB *bb, unsigned int type)
{
	bool ret = false;
	vector<pair<unsigned int, BB *> > lcstack;
	pair<unsigned int, BB *> elem;
	unsigned int depth = 0;

	lcstack = cstack;
	while (!lcstack.empty()) {
		depth++;
		elem = lcstack.back();
		lcstack.pop_back();

		if (elem.first == type)
			ret = true;

		bb->cfg_add_out(new CFGEdge(bb, elem.second, lcstack, false, depth));
	}

	return ret;
}

void
cfg_add_outgoing(BB *bb, BB *fallthrough_bb, Instruction *insn)
{
	/* Instructions have either 1 or 2 branch targets. CMASK, RET, BRK
	 * can trigger a CSTACK unwind.
	 */
	BB *dst;
	pair<unsigned int, BB *> elem;
	vector<pair<unsigned int, BB *> > lcstack;
	bool cstack_err = true;

	dst = insn->getBranchTakenDst();
	if (dst) {
		bb->cfg_add_out(new CFGEdge(bb, dst, cstack, true));
	}

	if (insn->canBranchNotTaken())
		bb->cfg_add_out(new CFGEdge(bb, fallthrough_bb, cstack));

	switch (insn->getOp()) {
	case OP_CPOP:
		elem = cstack.back();
		cstack.pop_back();
		bb->cfg_add_out(new CFGEdge(bb, elem.second, cstack, true));
		break;
	case OP_BRA:
		/* Could end up injecting a CPOP straight away. */
		lcstack = cstack;
		elem = cstack.back();
		lcstack.pop_back();
		bb->cfg_add_out(new CFGEdge(bb, elem.second, lcstack, false, 1));
		break;
	case OP_CALL:
		/* Could end up immediately injecting a CPOP to return.*/
		lcstack = cstack;
		elem = cstack.back();
		lcstack.pop_back();
		/** XXX: validate this edge requires the jump to finish too. */
		bb->cfg_add_out(new CFGEdge(bb, elem.second, lcstack, true, 1));
		break;
	case OP_BRK:
		cstack_err = cfg_add_out_cstack(bb, VSP_CTRL_BREAK);
		break;
	case OP_RET:
		cstack_err = cfg_add_out_cstack(bb, VSP_CTRL_RET);
		break;
	case OP_CMASK:
		cstack_err = cfg_add_out_cstack(bb, VSP_CTRL_RUN);
		break;
	default:
		break;
	}

	if (!cstack_err)
		cerr << "BB(" << bb->get_id() << "): CSTACK mask modification"
		    " instruction found without matching stack entry." << endl;

	return;
}

bool
CFA_validate_cstack(Program &p)
{
	bool ret = true;
	vector<BB *>::const_iterator bbit;
	BB *bb;
	BB *src;

	for (bbit = p.cbegin(); bbit != p.cend(); bbit++) {
		bb = *(bbit);
		/* Take first edge as validation point.*/
		list<CFGEdge *>::const_iterator it = bb->cfg_in_begin();
		if (it == bb->cfg_in_end())
			continue;

		vector<pair<unsigned int, BB *> > &cstack_a = (*it)->getCSTACK();
		src = (*it)->getSrc();

		for (it++; it != bb->cfg_in_end(); it++) {
			vector<pair<unsigned int, BB *> > &cstack_b =
					(*it)->getCSTACK();
			if (!cstack_equal(cstack_a, cstack_b)) {
				ret = false;
				cerr << "CSTACK incoming CFG mismatch between BB("
					<< src->get_id() << ") and BB("
					<< (*it)->getSrc()->get_id() << ")." << endl;
			}
		}
	}

	if (!ret)
		cerr << "Info: WCET analysis currently requires all incoming "
			"CFG edges for a BB to have the same stack state. This "
			"prevents some forms of code-sharing (e.g. functions). "
			"Lifting this restriction is left as future work.";

	return ret;
}

void
CFA_update_cpush(BB *fallthrough_bb, Instruction *insn)
{
	unsigned int cs_type = VSP_CTRL_RUN;
	BB *cs_target;

	switch (insn->getOp()) {
	case OP_CPUSH:
		cs_type = unsigned(insn->getSubOp().cpush);
		cs_target = insn->getSrc(0).getTargetBB();
		break;
	case OP_BRA:
		cs_type = VSP_CTRL_RUN;
		cs_target = insn->getSrc(0).getTargetBB();
		break;
	case OP_CALL:
		cs_type = VSP_CTRL_RET;
		cs_target = fallthrough_bb;/* The next BB. */
		break;
	default:
		return;
		break;
	}

	cstack.push_back(pair<unsigned int, BB*>(cs_type, cs_target));
	cstack_max_depth = max(cstack_max_depth, cstack.size());

	return;
}

/** Extract a (nested) list of loops from the CFG.
 * @param p Program to analyse and store outer loops into. */
void
CFA_loops(Program &p)
{
	vector<BB *>::const_reverse_iterator bbit;
	list<CFGEdge *>::const_iterator eit;
	BB *bb;
	CFGEdge *e;
	Loop *l;

	cur_loop = nullptr;

	for (bbit = p.crbegin(); bbit != p.crend(); bbit++) {
		bb = *bbit;

		for (eit = bb->cfg_out_begin();
				eit != bb->cfg_out_end(); eit++) {
			e = *eit;

			if (e->getDst()->get_id() < bb->get_id()) {
				l = new Loop(e->getDst(), bb, cur_loop);

				if (cur_loop)
					cur_loop->nest(l);
				else
					p.addLoop(l);

				cur_loop = l;
			}

			/* If this is the start of this loop (and potential
			 * parent loops), traverse upwards.*/
			while (cur_loop &&
			       cur_loop->getStart()->get_id() >= bb->get_id())
				cur_loop = cur_loop->getParent();
		}
	}
}

void
ControlFlow(Program &p)
{
	list<Instruction *>::iterator it, next_it;
	list<Instruction *>::reverse_iterator rit;
	vector<BB *>::const_iterator bbit;
	vector<BB *>::const_iterator next_bbit;
	BB *bb;
	BB *next_bb;
	Instruction *last_insn = nullptr;

	/* There's a few things to do here.
	 * - Fold exit into the last store,
	 * - Create CFGEdge BB->BB edges.
	 * - Track the stack
	 */

	cstack_max_depth = 0;

	for (bbit = p.cbegin(); bbit != p.cend(); bbit++) {
		bb = *bbit;
		next_bbit = bbit+1;
		if (next_bbit != p.cend())
			next_bb = *next_bbit;
		else
			next_bb = nullptr;

		it = bb->begin();

		/** XXX: folding exit into last store should be a separate pass,
		 * as it's the only bit of control flow analysis required by
		 * the main simulator. */
		if (last_insn && (*it)->getOp() == OP_EXIT &&
		    (*it)->getSrcs() == 0) {
			last_insn->setExit();
		}

		for (; it != bb->end(); it++) {
			/* Track the stack here */
			last_insn = (*it);
			CFA_update_cpush(next_bb, last_insn);
		}

		if (next_bb)
			cfg_add_outgoing(bb, next_bb, last_insn);
	}

	/* Next pass: validate stack on CFG edges. */
	CFA_validate_cstack(p);

	/* Extract all loops. */
	CFA_loops(p);
}

size_t
ControlFlow_CSTACKMaxDepth(void)
{
	return cstack_max_depth;
}

}
