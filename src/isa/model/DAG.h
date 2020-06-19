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

#ifndef ISA_MODEL_DAG_H
#define ISA_MODEL_DAG_H

#include <vector>
#include <utility>
#include <iostream>
#include <map>

#include "isa/model/BB.h"

namespace isa_model {

typedef enum {
	XS_NONE,
	XS_DRAM,
	XS_SP
} xs_type_t;

/** Node in a directed acyclic graph, describing abstractly the cost of each
 * BB's compute and DRAM request.
 */
class DAGNode {
private:
	/** Static counter used to assign unique IDs to each DAGNode. */
	static unsigned int id_counter;

	/** List of outgoing edges as (cost, sink) pair. */
	std::vector<std::pair<unsigned long,DAGNode *> > out;

	/** Identifier of this particular DAGNode. */
	unsigned int id;
	/** Basic block associated with this node. */
	BB *bb;
	/** Access type. */
	xs_type_t xs_type;
	/** WCET of the DRAM or scratchpad access at the end of this node. */
	unsigned long xs_wcet;

	/** Number of incoming edges. */
	unsigned int indegree;
	/** Number of active incoming edges during a graph traversal. Used to
	 * naturally enforce topological order of traversal. */
	unsigned int visited;

	/** Predecessor on the critical path. */
	DAGNode *critical_path_predecessor;
	/** Cost of the edge on critical path. Cached value for easier
	 * reconstruction. */
	unsigned long critical_path_predecessor_edge_cost;
	/** Current cumulative critical path cost from source to this node. */
	unsigned long critical_path_cost;

	/** True iff this DAGNode is part of an expanded loop. */
	bool expanded_loop;

public:
	/** Generate a unique DAGNode ID.
	 * @return A unique DAGNode ID. */
	static unsigned int uniqueId(void);

	/** Constructor
	 * @param bb Basic block to construct this DAGNode for.
	 * @param xst Access type associated with this DAGNode (DRAM, SP, none).
	 * @param dwcet Worst-case execution time of the DRAM request associated
	 * 		with this DAG node.
	 */
	DAGNode(BB *bb, xs_type_t xst, unsigned long dwcet);

	/** Return the unique ID for this DAGNode.
	 * @return The unique ID for this DAGNode. */
	unsigned int getId(void) const;

	/** Return a pointer to the BB associated with this DAGNode.
	 * @return A pointer to the BB associated with this DAGNode. */
	BB *getBB(void) const;

	/** Return the associated BB id.
	 * @return The associated BB id. */
	unsigned int getBBid(void) const;

	/** Get the type of access performed by instruction terminating the BB
	 * for this DAGNode.
	 * @return The access type performed by the BB for this DAGNode. */
	xs_type_t getXSType(void) const;

	/** Return the cost for the access associated with this DAGNode, 0 if
	 * no access is performed.
	 * @return The cost of the access for this DAGNode in compute cycles. */
	unsigned long getXSCost(void) const;

	/** Add an outgoing edge to this node
	 * @param cwcet Worst-case execution time for compute.
	 * @param n Destination of outgoing edge. */
	void addOut(unsigned long cwcet, DAGNode *n);

	/** Reset the active incoming edges counter. */
	void resetVisited(void);

	/** Visit a node, set or update the critical path cost.
	 * @param n The visiting incoming DAGNode.
	 * @param edge_cost The cost of the incoming edge from n.
	 * @param cost The cumulative worst-case cost from source to &this when
	 * 		visited through n.
	 * @return True iff no more incoming edges are active. */
	bool visit(DAGNode *n, unsigned int edge_cost, unsigned long cost);

	/** Return the cumulative critical path cost, set by updateCriticalPath.
	 * @return Critical path cumulative cost.*/
	unsigned long getCriticalPathCost(void) const;

	/** Obtain the critical path predecessor for this node.
	 * @return The critical path predecessor DAGNode for this node. */
	DAGNode *getCriticalPathPredecessor(void) const;

	/** Obtain the critical path predecessor for this node.
	 * @return The cost of the edge from the critical path predecessor to
	 * 	   this node. */
	unsigned long getCriticalPathPredecessorEdgeCost(void) const;

	/** Retrieve the constant iterator begin for outgoing nodes.
	 * @return Const-iterator over all outgoing edges from this DAGNode. */
	std::vector<std::pair<unsigned long, DAGNode*> >::const_iterator
			out_cbegin(void) const;

	/** Retrieve the constant iterator end for outgoing nodes.
	 * @return Const-iterator dummy end element for this DAGNode. */
	std::vector<std::pair<unsigned long, DAGNode*> >::const_iterator
			out_cend(void) const;

	/** Return The number of outgoing edges from this DAGNode.
	 * @return the number of outgoing edges from this DAGNode. */
	unsigned int outCount(void) const;

	/** Set the boolean indicating this DAGNode is part of an expanded
	 * (unrolled) loop.*/
	void setExpandedLoop(void);

	/** Return true iff this DAGNode is part of an expanded (unrolled) loop.
	 * @return True iff this DAGNode is part of an expanded (unrolled) loop.*/
	bool getExpandedLoop(void) const;

	/** Print the BB properties.
	 * @param os Output stream.
	 * @param v DAGNode to print.
	 * @return Output stream. */
	inline friend std::ostream &
	operator<<(std::ostream &os, DAGNode &v)
	{
		os << v.id << ": DAGNode BB(" << v.bb->get_id() <<") ";
		switch(v.xs_type) {
		case XS_DRAM:
			os << "DRAM(" << v.xs_wcet << ") ";
			break;
		case XS_SP:
			os << "SP(" << v.xs_wcet << ") ";
			break;
		default:
			break;
		}

		os << "CRIT(" << v.critical_path_cost << ")" << std::endl;

		for (auto &o : v.out)
			os << "  -> "<< o.second->getBBid() << ": compute(" <<
					o.first << ")" << std::endl;
		if (v.critical_path_predecessor)
			os << "  <- " << v.critical_path_predecessor->getBBid()
			<< ": compute(" << v.critical_path_predecessor_edge_cost
			<< ")" << std::endl;

		return os;
	}

	/** Copy a node, excluding edges.
	 * @param strip_sp Remove any SP related cost?
	 * @return The new DAGNode. */
	DAGNode *copyNode(bool strip_sp = false);
};

/** Directed acyclic graph of BBs. */
class DAG {
private:
	/** DAG source node. */
	DAGNode *source;

	/** DAG sink node. */
	DAGNode *sink;

	/** List of pointers to all nodes in this DAG. */
	std::vector<DAGNode *> nodes;

	/** A map from BB id to DAGNode.
	 *
	 * Note that a single BB may point to multiple DAG nodes, e.g. when
	 * unrolling loops. This map will contain the last node added for
	 * a given BB. */
	std::map<unsigned int, DAGNode *> bbnode;

public:
	/** Default empty constructor. */
	DAG();

	/** Destructor. */
	~DAG();

	/** Retrieve a DAG node from the bbnode map for given BB.
	 * @param bb BB to find the DAG node for
	 * @return The associated DAGNode for bb, nullptr if no BB is present
	 * in the map with the same BB id. */
	DAGNode *get(BB *bb);

	/** Finds a DAGNode for given BB in the bbnode map, creates a new one
	 * if it doesn't yet exist.
	 * @param bb BB node to find or create a DAG node for.
	 * @return The associated DAGNode for bb. */
	DAGNode *ensure(BB *bb);

	/** Clears the BB-to-DAG node mapping, preparing it for another loop
	 * iteration. */
	void clearBBMap(void);

	/** Sets the source DAGNode to an entry in the bbnode map.
	 * @param bb BB identifier. */
	void setSource(unsigned int bb);

	/** Sets the sink DAGNode to an entry in the bbnode map.
	 * @param bb BB identifier */
	void setSink(unsigned int bb);

	/** Unsets the sink DAGNode. */
	void unsetSink(void);

	/** Reset all DAGNodes' active incoming edge counters to their total
	 * incoming edges. */
	void resetVisited(void);

	/** Return the source DAGNode.
	 * @return The source DAGNode. */
	DAGNode *getSource(void);

	/** Return the sink DAGNode.
	 * @return The sink DAGNode. */
	DAGNode *getSink(void);

	/** Count the total number of nodes in this DAG.
	 * @return The number of DAGNodes in this DAG. */
	unsigned int countNodes(void) const;

	/** Append this DAG to the end of DAG d.
	 * @param d DAG to append this DAG to. */
	void append(DAG *d);

	/** Copy the node information of a given DAGNode into this DAG.
	 * @param n DAGNode to copy into this DAG.
	 * @param strip_sp Remove scratchpad access information from the
	 * 		   DAGNode.
	 * @return Copy of the node. */
	DAGNode *copyInto(DAGNode *n, bool strip_sp = false);

	/** Print the DAG properties.
	 * @param os Output stream.
	 * @param v The DAG to print.
	 * @return Output stream. */
	inline friend std::ostream &
	operator<<(std::ostream &os, DAG &v)
	{
		for (DAGNode *n : v.nodes)
			os << *n << std::endl;

		return os;
	}
};

}

#endif /* ISA_MODEL_DAG_H */
