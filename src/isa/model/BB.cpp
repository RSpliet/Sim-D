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

#include "isa/model/BB.h"
#include "isa/model/CFGEdge.h"

using namespace isa_model;

BB::BB() : id(0), pc(0), exec_cycles(0) {}

BB::BB(unsigned int i, unsigned int p) : id(i), pc(p), exec_cycles(0) {}

BB::~BB()
{
	Instruction *i;

	while (insns.size()) {
		i = insns.front();
		insns.pop_front();
		delete i;
	}
}

bool
BB::empty(void) const
{
	return insns.empty();
}

void
BB::add_instruction(Instruction *i)
{
	i->setBB(id);
	insns.push_back(i);
}

unsigned int
BB::countInstructions(void) const
{
	return insns.size();
}

void
BB::cfg_add_in(CFGEdge *bb)
{
	cfg_in.push_back(bb);
	bb->getSrc()->cfg_out.push_back(bb);
}

void
BB::cfg_add_out(CFGEdge *bb)
{
	cfg_out.push_back(bb);
	bb->getDst()->cfg_in.push_back(bb);
}

list<Instruction *>::iterator
BB::begin(void)
{
	return insns.begin();
}

list<Instruction *>::iterator
BB::end()
{
	return insns.end();
}

list<Instruction *>::reverse_iterator
BB::rbegin(void)
{
	return insns.rbegin();
}

list<Instruction *>::reverse_iterator
BB::rend()
{
	return insns.rend();
}

sc_uint<11>
BB::get_pc(void) const
{
	return pc;
}

unsigned int
BB::get_pc_uint(void) const
{
	return pc.to_uint();
}

unsigned int
BB::get_id(void) const
{
	return id;
}

void
BB::set_pc(sc_uint<11> p)
{
	pc = p;
}

void
BB::printCFG(ostream &os) const
{
	list<CFGEdge *>::const_iterator it;

	for (it = cfg_in.begin(); it != cfg_in.end(); it++) {
		os << "  <- BB(" << (*it)->getSrc()->id << ")";
		(*it)->printCSTACK(os);
		os << endl;
	}

	for (it = cfg_out.begin(); it != cfg_out.end(); it++) {
		os << "  -> BB(" << (*it)->getDst()->id << ")";

		if ((*it)->isJump())
			os << " jump";

		if ((*it)->CPOPCount())
			os << " CPOP(" << (*it)->CPOPCount() << ")";

		(*it)->printCSTACK(os);

		os << endl;
	}
}

void
BB::setExecCycles(unsigned long cyc, bool warm)
{
	if (warm)
		exec_cycles_warm = cyc;
	else
		exec_cycles = cyc;
}

unsigned long
BB::getExecCycles(void) const
{
	return exec_cycles;
}

unsigned long
BB::getPipelinePenalty(void) const
{
	return exec_cycles_warm - exec_cycles;
}

list<CFGEdge *>::const_iterator
BB::cfg_in_begin(void) const
{
	return cfg_in.cbegin();
}

list<CFGEdge *>::const_iterator
BB::cfg_in_end(void) const
{
	return cfg_in.cend();
}


list<CFGEdge *>::const_iterator
BB::cfg_out_begin(void) const
{
	return cfg_out.cbegin();
}

list<CFGEdge *>::const_iterator
BB::cfg_out_end(void) const
{
	return cfg_out.cend();
}

bool
BB::mayTakeBranch(void) const
{
	Instruction *op = insns.back();

	if (!op)
		return false;

	return op->mayTakeBranch();
}

bool
BB::mayTakeFallthrough(void) const
{
	Instruction *op = insns.back();

	if (!op)
		return true;

	return op->mayTakeFallthrough();
}

void
BB::incrementBranchCycle(void)
{
	Instruction *op = insns.back();

	if (!op)
		return;

	op->incrementBranchCycle();
}

void
BB::resetBranchCycle(void)
{
	Instruction *op = insns.back();

	if (!op)
		return;

	op->resetBranchCycle();
}
