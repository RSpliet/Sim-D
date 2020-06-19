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

#include "isa/analysis/TimingDAG.h"
#include "util/debug_output.h"

#include <map>
#include <unordered_map>
#include <deque>
#include <stdexcept>
#include <stack>

using namespace isa_model;
using namespace std;

namespace isa_analysis {

static unordered_map<unsigned int, Loop *> expanded_loops;

Loop *
getLoop(BB *bb)
{
	unordered_map<unsigned int, Loop *>::iterator lit;

	lit = expanded_loops.find(bb->get_id());
	if (lit != expanded_loops.end())
		return lit->second;

	return nullptr;
}

DAGNode *
ensure(DAG *dag, BB *bb)
{
	Loop *lit;
	DAGNode *dn;

	dn = dag->get(bb);
	if (dn)
		return dn;

	lit = getLoop(bb);
	if (lit) {
		dag->append(lit->getDAG());
		dn = dag->get(bb);
	} else {
		dn = dag->ensure(bb);
	}

	return dn;
}

BB *
endOfLoopBB(BB *bb)
{
	unordered_map<unsigned int, Loop *>::iterator lit;
	lit = expanded_loops.find(bb->get_id());

	if (lit != expanded_loops.end()) {
		return lit->second->getDAG()->getSink()->getBB();
	}

	return bb;
}

void
expandRange(DAG *dag, BB *source, BB *sink)
{
	deque<BB *> wq;
	BB *bb;
	BB *bbt;
	DAGNode *dn;
	DAGNode *dnt;
	list<CFGEdge *>::const_iterator edgeit;
	CFGEdge *edge;
	CFGEdge_type et;
	unsigned int sink_id;
	Loop *loop;

	sink_id = sink->get_id();
	dag->clearBBMap();

	/* If this is the nth iteration of a loop, connect the sink to the
	 * source of the 2nd iteration. */
	dn = dag->getSink();
	if (dn) {
		dnt = ensure(dag, source);

		for (edgeit = sink->cfg_out_begin();
		     edgeit != sink->cfg_out_end(); edgeit++) {
			edge = *edgeit;
			if (edge->getDst() != source)
				continue;

			dn->addOut(sink->getExecCycles() + edge->getCycles(), dnt);
		}

		dag->unsetSink();
	}

	wq.push_back(source);

	/* Now traverse the entire subprogram, never entering a BB twice. */
	while (!wq.empty()) {
		bb = wq.front();
		wq.pop_front();

		loop = getLoop(bb);
		ensure(dag, bb);
		bb = endOfLoopBB(bb);
		dn = dag->get(bb);

		/** When metadata exists, we shouldn't iterate but selectively
		 * pick an edge. */
		for (edgeit = bb->cfg_out_begin(); edgeit != bb->cfg_out_end();
				edgeit++) {
			edge = *edgeit;
			bbt = edge->getDst();
			et = edge->getType();

			if (et == EDGE_CTRLFLOW &&
			    bbt->get_id() == sink->get_id() + 1) {
				/* Special case: early loop exit. */
				if (!bb->mayTakeBranch())
					    continue;

				sink_id = bb->get_id();
				break;
			} else if (bbt->get_id() < source->get_id() ||
			    bbt->get_id() > sink->get_id()) {
				cerr << "Error: Jump outside range detected." << endl;
				throw invalid_argument("Error: Jump outside range detected.");
			}

			if (dn->getExpandedLoop()) {
				/* We just want to catch the loop-exit edge. */
				assert(loop);
				if ((bb->get_id() == loop->getEnd()->get_id() && et != EDGE_FALLTHROUGH) ||
				    (bb->get_id() != loop->getEnd()->get_id() && et != EDGE_CTRLFLOW))
					continue;
			} else if ((et == EDGE_CTRLFLOW && !bb->mayTakeBranch()) ||
				   (et != EDGE_CTRLFLOW && !bb->mayTakeFallthrough())) {
					continue;
			}

			dnt = dag->get(bbt);
			if (!dnt) {
				/* Doesn't exist, should appear on the work-queue */
				dnt = ensure(dag, bbt);
				if (bbt != sink)
					wq.push_back(bbt);
			}

			dn->addOut(bb->getExecCycles() + edge->getCycles(), dnt);
		}

		if (bb->get_id() != sink_id)
			bb->incrementBranchCycle();
	}

	if (!dag->getSource())
		dag->setSource(source->get_id());
	dag->setSink(sink_id);
}

void
resetBranchCycles(Program &p, BB *source, BB *sink)
{
	unsigned int i;

	for (i = source->get_id(); i <= sink->get_id(); i++)
		p.getBB(i)->resetBranchCycle();
}

DAG *
criticalPath(DAG *in, bool sp_as_compute)
{
	DAG *dag;
	DAGNode *n;
	DAGNode *cpn;
	DAGNode *cpn_sink;
	deque<DAGNode *> wq;
	pair<unsigned long, DAGNode *> d;
	vector<pair<unsigned long, DAGNode *>>::const_iterator dit;
	bool lastInEdge;
	unsigned long node_cost;

	if (debug_output[DEBUG_WCET_PROGRESS]) {
		cout << "* DAG critical path determination. ";
		if (sp_as_compute)
			cout << "(SP as execute)" << endl;
		else
			cout << "(SP as access)" << endl;
	}

	dag = new DAG();

	in->resetVisited();

	/* We use a work-queue in combination with per-node incoming edge
	 * counters to naturally iterate this DAG in topological order. */
	wq.push_back(in->getSource());

	while (!wq.empty()) {
		n = wq.front();
		wq.pop_front();

		for (dit = n->out_cbegin(); dit != n->out_cend(); dit++) {
			d = *dit;
			node_cost = d.first;

			if (sp_as_compute && n->getXSType() == XS_SP)
				node_cost += n->getXSCost();

			lastInEdge = d.second->visit(n, node_cost,
					n->getCriticalPathCost() + node_cost);

			if (lastInEdge)
				wq.push_back(d.second);
		}
	}

	/* Extract the critical path. Copying seems easier than culling. */
	n = in->getSink();
	cpn_sink = dag->copyInto(n, sp_as_compute);

	do {
		n = n->getCriticalPathPredecessor();
		cpn = dag->copyInto(n, sp_as_compute);
		cpn->addOut(cpn_sink->getCriticalPathPredecessorEdgeCost(),
				cpn_sink);

		cpn_sink = cpn;
	} while (n != in->getSource());

	dag->setSource(in->getSource()->getBBid());
	dag->setSink(in->getSink()->getBBid());

	if (debug_output[DEBUG_WCET_PROGRESS]) {
		cout << "  Nodes in : " << in->countNodes() << endl;
		cout << "  Nodes out: " << dag->countNodes() << endl;
	}

	return dag;
}

bool
nextIteration(BB *end, BB *bb)
{
	bool retval;

	if (end == bb)
		retval = bb->mayTakeBranch();
	else
		retval = bb->mayTakeFallthrough();
	bb->incrementBranchCycle();

	return retval;
}

/* Recursive, depth shouldn't be a problem. */
void
processLoopTree(Loop *l, unsigned int level = 0)
{
	vector<Loop *>::const_iterator cit;
	DAG *dag;
	BB *start;
	BB *end;
	BB *sink;

	for (cit = l->cbegin(); cit != l->cend(); cit++) {
		processLoopTree(*cit, level + 1);
	}

	if (!l->getDAG()) {
		start = l->getStart();
		end = l->getEnd();

		if (debug_output[DEBUG_WCET_PROGRESS])
			cout << "  Expandng loop " << start->get_id() << " -> "
					<< end->get_id() <<  endl;
		dag = new DAG();

		do {
			expandRange(dag, start, end);
			sink = dag->getSink()->getBB();
		} while (nextIteration(end, sink));

		l->setDAG(dag);
	}

	expanded_loops[l->getStart()->get_id()] = l;
}

DAG *
TimingDAG(Program &prg)
{
	DAG *dag;
	vector<Loop *>::const_iterator lit;

	if (debug_output[DEBUG_WCET_PROGRESS])
		cout << "* CFG to DAG transformation." << endl;

	resetBranchCycles(prg, prg.getBB(0), prg.getBB(prg.getBBCount() - 1));
	for (lit = prg.loops_cbegin(); lit != prg.loops_cend(); lit++) {
		processLoopTree(*lit);
	}

	dag = new DAG();
	expandRange(dag, prg.getBB(0), prg.getBB(prg.getBBCount() - 1));

	expanded_loops.clear();

	if (debug_output[DEBUG_WCET_PROGRESS]) {
		cout << "DAG:" << endl;
		cout << *dag << endl;
	}

	return dag;
}

}
