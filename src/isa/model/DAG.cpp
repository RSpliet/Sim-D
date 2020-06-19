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

#include "isa/model/DAG.h"
#include "isa/model/BB.h"

#include <deque>
#include <unordered_map>

using namespace std;
using namespace isa_model;

unsigned int DAGNode::id_counter = 0;

unsigned int
DAGNode::uniqueId(void)
{
	return id_counter++;
}

DAGNode::DAGNode(BB *b, xs_type_t xst, unsigned long xswcet)
: bb(b), xs_type(xst), xs_wcet(xswcet), indegree(0),
  visited(0), critical_path_predecessor(nullptr),
  critical_path_predecessor_edge_cost(0), critical_path_cost(0),
  expanded_loop(false)
{
	id = DAGNode::uniqueId();
}

unsigned int
DAGNode::getId(void) const
{
	return id;
}

BB *
DAGNode::getBB(void) const
{
	return bb;
}

unsigned int
DAGNode::getBBid(void) const
{
	return bb->get_id();
}

xs_type_t
DAGNode::getXSType(void) const
{
	return xs_type;
}


unsigned long
DAGNode::getXSCost(void) const
{
	return xs_wcet;
}

void
DAGNode::addOut(unsigned long cwcet, DAGNode *n)
{
	out.emplace_back(pair<unsigned long, DAGNode *>(cwcet, n));

	n->indegree++;
}

void
DAGNode::resetVisited(void)
{
	visited = 0;
}

bool
DAGNode::visit(DAGNode *n, unsigned int edge_cost, unsigned long cost)
{
	visited++;

	if (cost > critical_path_cost) {
		critical_path_cost = cost;
		critical_path_predecessor = n;
		critical_path_predecessor_edge_cost = edge_cost;
	}

	return (visited == indegree);
}

unsigned long
DAGNode::getCriticalPathCost(void) const
{
	return critical_path_cost;
}

DAGNode *
DAGNode::getCriticalPathPredecessor(void) const
{
	return critical_path_predecessor;
}

unsigned long
DAGNode::getCriticalPathPredecessorEdgeCost(void) const
{
	return critical_path_predecessor_edge_cost;
}

vector<pair<unsigned long, DAGNode*> >::const_iterator
DAGNode::out_cbegin(void) const
{
	return out.cbegin();
}

vector<pair<unsigned long, DAGNode*> >::const_iterator
DAGNode::out_cend(void) const
{
	return out.cend();
}

unsigned int
DAGNode::outCount(void) const
{
	return out.size();
}

void
DAGNode::setExpandedLoop(void)
{
	expanded_loop = true;
}

bool
DAGNode::getExpandedLoop(void) const
{
	return expanded_loop;
}

DAGNode *
DAGNode::copyNode(bool strip_sp)
{
	DAGNode *n;
	if (strip_sp && xs_type == XS_SP)
		n = new DAGNode(bb, XS_NONE, 0);
	else
		n = new DAGNode(bb, xs_type, xs_wcet);

	/* Copy in to keep handy as we clone paths. */
	n->critical_path_predecessor_edge_cost =
			critical_path_predecessor_edge_cost;

	return n;
}

DAG::DAG()
: source(nullptr), sink(nullptr) {}

DAG::~DAG()
{
	source = nullptr;
	sink = nullptr;

	for (DAGNode *n : nodes) {
		delete n;
	}

	nodes.clear();
	bbnode.clear();
}

DAGNode *
DAG::get(BB *bb)
{
	DAGNode *n;

	n = nullptr;

	if (bbnode.find(bb->get_id()) != bbnode.end())
		n = bbnode[bb->get_id()];

	return n;
}

DAGNode *
DAG::ensure(BB *bb)
{
	DAGNode *n;
	xs_type_t xst;
	Instruction *op;
	unsigned long dwcet;

	n = get(bb);

	if (!n) {
		op = *(bb->rbegin());

		if (op->ldst() &&
		    op->getMetadata()) {
			dwcet = op->getMetadata()->getDRAMlid();
			xst = (op->ldstsp() ? XS_SP : XS_DRAM);
		} else {
			dwcet = 0ul;
			xst = XS_NONE;
		}

		n = new DAGNode(bb, xst, dwcet);
		bbnode[bb->get_id()] = n;
		nodes.push_back(n);
	}

	return n;
}

void
DAG::clearBBMap(void)
{
	bbnode.clear();
}

DAGNode *
DAG::copyInto(DAGNode *n, bool strip_sp)
{
	DAGNode *retn;

	retn = n->copyNode(strip_sp);
	nodes.push_back(retn);
	if (n->getExpandedLoop())
		retn->setExpandedLoop();
	else
		/* XXX This is a bit of a hack to help critical path analysis
		 * find its head and tail end, under the assumption that the
		 * first and last BB aren't part of a loop. The last BB can't
		 * be, as it has to contain a DRAM store. The first never is in
		 * practice because most kernels first check whether threads are
		 * out of bounds. */
		bbnode[retn->getBBid()] = retn;

	return retn;
}

void
DAG::setSource(unsigned int bb)
{
	source = bbnode.at(bb);
}

void
DAG::setSink(unsigned int bb)
{
	sink = bbnode.at(bb);
}

void
DAG::unsetSink(void)
{
	sink = nullptr;
}

DAGNode *
DAG::getSource(void)
{
	return source;
}

DAGNode *
DAG::getSink(void)
{
	return sink;
}

unsigned int
DAG::countNodes(void) const
{
	return nodes.size();
}

/* We need to map to remote DAGNode IDs, not BBs, which means we can't re-use
 * any of the existing helpers safely. */
void
DAG::append(DAG *dag)
{
	DAGNode *n;
	DAGNode *n_sink;
	DAGNode *cpn;
	DAGNode *cpn_sink;
	deque<DAGNode *> wq;
	pair<unsigned long, DAGNode *> d;
	vector<pair<unsigned long, DAGNode *>>::const_iterator dit;
	unordered_map<unsigned int, DAGNode *> visited;

	/* Work-queue will help naviate this DAG. Don't visit DAGNodes twice. */
	wq.push_back(dag->getSource());

	while (!wq.empty()) {
		n = wq.front();
		wq.pop_front();

		if (visited.find(n->getId()) == visited.end()) {
			cpn = copyInto(n);
			cpn->setExpandedLoop();
			visited[n->getId()] = cpn;
		} else {
			cpn = visited[n->getId()];
		}

		for (dit = n->out_cbegin(); dit != n->out_cend(); dit++) {
			d = *dit;
			n_sink = d.second;

			if (visited.find(n_sink->getId()) == visited.end()) {
				cpn_sink = copyInto(n_sink);
				cpn_sink->setExpandedLoop();
				visited[n_sink->getId()] = cpn_sink;
				wq.push_back(n_sink);
			} else {
				cpn_sink = visited[n_sink->getId()];
			}

			cpn->addOut(d.first, cpn_sink);
		}
	}

	/* Make the edges of the in-lined DAG findable by BB. */
	bbnode[dag->getSource()->getBBid()] = visited[dag->getSource()->getId()];
	bbnode[dag->getSink()->getBBid()] = visited[dag->getSink()->getId()];
}

void
DAG::resetVisited(void)
{
	for (DAGNode *n : nodes) {
		n->resetVisited();
	}
}
