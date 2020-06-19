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

#include "isa/model/CFGEdge.h"

using namespace isa_model;

CFGEdge::CFGEdge(BB *s, BB *d, vector<pair<unsigned int, BB *> > &cs,
			bool j, unsigned int cp)
: src(s), dst(d), cycles(0), cpops(cp), cstack(cs)
{
	if (j)
		type = EDGE_CTRLFLOW;
	else if (cpops > 0)
		type = EDGE_CPOP_INJECTED;
	else
		type = EDGE_FALLTHROUGH;
}

BB *
CFGEdge::getSrc(void)
{
	return src;
}

BB *
CFGEdge::getDst(void)
{
	return dst;
}

void
CFGEdge::setCycles(unsigned long cyc)
{
	cycles = cyc;
}

unsigned long
CFGEdge::getCycles(void) const
{
	return cycles;
}

bool
CFGEdge::isJump(void)
{
	return type == EDGE_CTRLFLOW;
}

unsigned int
CFGEdge::CPOPCount(void)
{
	return cpops;
}

CFGEdge_type
CFGEdge::getType(void) const
{
	return type;
}

void
CFGEdge::printCSTACK(ostream &os)
{
	bool printComma = false;

	os << " CSTACK(";
	for (auto &cstack_entry : cstack) {
		if (printComma)
			os << ",";

		os << "<";
		switch(cstack_entry.first) {
		case VSP_CTRL_RUN:
			os << "ctrl";
			break;
		case VSP_CTRL_BREAK:
			os << "brk";
			break;
		case VSP_CTRL_RET:
			os << "ret";
			break;
		default:
			os << "ERROR";
		}

		os << ",BB(" << cstack_entry.second->get_id() << ")>";

		printComma = true;
	}
	os << ") WCET("	<< cycles << ")";
}

vector<pair<unsigned int, BB *> > &
CFGEdge::getCSTACK(void)
{
	return cstack;
}
