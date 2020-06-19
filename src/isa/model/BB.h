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

#ifndef ISA_MODEL_BB_H
#define ISA_MODEL_BB_H

#include <list>
#include <iterator>
#include <functional>

#include "isa/model/Instruction.h"

using namespace std;

namespace isa_model {

/** Forward definition */
class CFGEdge;

/** A basic block. */
class BB
{
private:
	/** BB identifier. */
	unsigned int id;

	/** PC of first instruction in this BB when emitted. */
	sc_uint<11> pc;

	/** List of instructions, ordered by appearance. */
	list<Instruction *> insns;

	/** Control flow graph: Incoming edges. */
	list<CFGEdge *> cfg_in;

	/** Control flow graph: Outgoing edges. */
	list<CFGEdge *> cfg_out;

	/** Number of cycles to execute this BB (excl. DRAM/SP access) with a
	 * warm pipeline. */
	unsigned long exec_cycles_warm;
	/** Number of cycles to execute this BB (excl. DRAM/SP access) with a
	 * cold pipeline. */
	unsigned long exec_cycles;

public:
	/** Default (empty) constructor. */
	BB();

	/** Proper constructor.
	 * @param i Identifier for this BB.
	 * @param p PC associated with the first instruction to be added to
	 *          this BB. */
	BB(unsigned int i, unsigned int p = 0);

	/** Default destructor */
	~BB();

	/** True iff the BB has no instructions.
	 * @return True iff the BB has zero instructions */
	bool empty(void) const;

	/** Insert an instruction into the BB.
	 * @param i Instruction to add. */
	void add_instruction(Instruction *i);

	/** Return the number of instructions. */
	unsigned int countInstructions(void) const;

	/** Add an in-edge to the control flow graph of this BB, and a
	 * corresponding out-edge into the provided bb.
	 * @param bb Basic block to point my out-edge to. */
	void cfg_add_in(CFGEdge *bb);

	/** Add an out-edge to the control flow graph of this BB, and a
	 * corresponding in-edge into the provided bb.
	 * @param bb Basic block to point my out-edge to. */
	void cfg_add_out(CFGEdge *bb);

	/** Get the start of the list of instructions as an iterator. */
	list<Instruction *>::iterator begin(void);

	/** Get the end of the list of instructions as an iterator object. */
	list<Instruction *>::iterator end(void);

	/** Get the last element of the list of instructions as an iterator. */
	list<Instruction *>::reverse_iterator rbegin(void);

	/** Get the end of the list of instructions as an iterator object. */
	list<Instruction *>::reverse_iterator rend(void);

	/** Get the PC of the first instruction in the BB */
	sc_uint<11> get_pc(void) const;

	/** Get the PC of the first instruction in the BB as a regular unsigned
	 * integer. */
	unsigned int get_pc_uint(void) const;

	/** Get the unique ID for this BB.
	 * @return the unique ID for this BB. */
	unsigned int get_id(void) const;

	/** Set the PC of the first instruction in this BB. Used for branch
	 * target resolution. */
	void set_pc(sc_uint<11> p);

	/** Print the contents of this BB in formatting compatible with the
	 * Control Flow Graph (CFG) print method.
	 * @param os Output stream. */
	void printCFG(ostream &os) const;

	/** Set the number of cycles required to execute this BB.
	 * @param cyc Number of cycles for executing the instructions of this
	 * 	       BB
	 * @param warm True iff this cycle count is a warm-pipeline run. False
	 * 	       for a cold-pipeline run.
	 */
	void setExecCycles(unsigned long cyc, bool warm);

	/** Get the number of cycles required to execute this BB with a cold
	 * pipeline.
	 * @return The number of cycles required to execute this BB with a cold
	 * 	   pipeline. */
	unsigned long getExecCycles(void) const;

	/** Get the pipeline penalty associated with a warm pipeline run.
	 * @return The worst-case penalty for warm-pipeline exec. of this BB. */
	unsigned long getPipelinePenalty(void) const;

	/** Const-iterator over the incoming edges for this BB.
	 * @return Const-iterator to first incoming edge. */
	list<CFGEdge *>::const_iterator cfg_in_begin() const;

	/** Const-iterator pointing to dummy last incoming edge.
	 * @return Const-iterator pointing to dummy last incoming edge. */
	list<CFGEdge *>::const_iterator cfg_in_end() const;

	/** Const-iterator over the outgoing edges for this BB.
	 * @return Const-iterator to first outgoing edge. */
	list<CFGEdge *>::const_iterator cfg_out_begin() const;

	/** Const-iterator pointing to dummy last outgoing edge.
	 * @return Const-iterator pointing to dummy last outgoing edge. */
	list<CFGEdge *>::const_iterator cfg_out_end() const;

	/** Return true iff this BB ends with a branch and it may be taken,
	 * either because no branch information is known or because annotations
	 * state it will.
	 * @return True iff this instruction may take a branch. */
	bool mayTakeBranch(void) const;

	/** Return true iff this BB either ends with something not a branch, or
	 * ends with a control flow instruction that may result in fall-through
	 * or CPOP, either because no branch information is known or because
	 * annotations state fallthrough is predicted.
	 * @return True iff this BB may fall through to the next BB (warm
	 * 	   pipeline). */
	bool mayTakeFallthrough(void) const;

	/** Increment the branchcycle counter for this BB. */
	void incrementBranchCycle(void);

	/** Reset the branchcycle counter for this BB to the default value
	 * provided by the branchcycle annotation. */
	void resetBranchCycle(void);

	/** Print the BB properties. */
	inline friend ostream &
	operator<<(ostream &os, BB &bb)
	{
		os << "BB(" << bb.id << ") PC(" << bb.pc << ") WCET(" << bb.exec_cycles << ")";

		return os;
	}
};

}

#endif /* ISA_MODEL_BB_H */
