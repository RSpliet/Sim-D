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

#ifndef ISA_MODEL_LOOP_H
#define ISA_MODEL_LOOP_H

#include "isa/model/BB.h"
#include "isa/model/DAG.h"

#include <vector>

namespace isa_model {

/** (For-while) loop element. */
class Loop {
private:
	/** Start of loop. */
	BB *start;
	/** End of loop. */
	BB *end;

	/** Loop parent. Nullptr if outer loop. */
	Loop *parent;
	/** List of loop children. */
	std::vector<Loop *> child;

	/** DAG implementing an expanded loop. */
	DAG *dag;

public:
	/** Constructor
	 * @param s Start of loop,
	 * @param e End of loop,
	 * @param p Parent of this loop. Nullptr if outer-loop.
	 */
	Loop(BB *s, BB *e, Loop *p);

	/** Return the parent loop for this loop, nullptr if no parent exists.
	 * @return parent loop.
	 */
	Loop *getParent(void) const;

	/** Return the start BB of this loop. */
	BB *getStart(void) const;

	/** Return the end BB of this loop. */
	BB *getEnd(void) const;

	/** Get the DAG resulting from the expansion/unrolling of this loop.
	 * @return The DAG resulting from expanding this loop. */
	DAG *getDAG(void) const;

	/** Set the DAG resulting from the expansion/unrolling of this loop.
	 * @param d The DAG resulting from expanding this loop. */
	void setDAG(DAG *d);

	/** Iterator over all children.
	 * @return Iterator iterating over all this loop's nested children. */
	std::vector<Loop *>::const_iterator cbegin(void);

	/** Iterator end over all children.
	 * @return Iterator element pointing at dummy end. */
	std::vector<Loop *>::const_iterator cend(void);

	/** Nest given loop into this loop. Throw an exception if improperly
	 * nested
	 * @param l Loop to nest into this one. */
	void nest(Loop *l);

	/** Output stream operator for Loop.
	 * @param os Output stream
	 * @param l Loop to print
	 * @return Output stream */
	inline friend std::ostream &
	operator<<(std::ostream &os, const Loop &l)
	{
		os << "Loop(" << l.start->get_id() << " -> " << l.end->get_id() <<
				")" << endl;
		for (Loop *c : l.child) {
			os << *c;
		}

		return os;
	}
};

}

#endif /* ISA_MODEL_LOOP_H */
