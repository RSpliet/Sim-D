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

#include "isa/model/Loop.h"

using namespace isa_model;

Loop::Loop(BB *s, BB *e, Loop *p)
: start(s), end(e), parent(p), dag(nullptr) {}

Loop *
Loop::getParent(void) const
{
	return parent;
}

BB *
Loop::getStart(void) const
{
	return start;
}

BB *
Loop::getEnd(void) const
{
	return end;
}

DAG *
Loop::getDAG(void) const
{
	return dag;
}

void
Loop::setDAG(DAG *d)
{
	dag = d;
}

void
Loop::nest(Loop *l)
{
	if (l->start->get_id() < start->get_id() ||
	    l->end->get_id() >= end->get_id()) {
		throw invalid_argument("Improperly nested loop.");
	}

	child.push_back(l);
}

vector<Loop *>::const_iterator
Loop::cbegin(void)
{
	return child.cbegin();
}

vector<Loop *>::const_iterator
Loop::cend(void)
{
	return child.cend();
}
