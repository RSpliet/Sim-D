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

#ifndef ISA_MODEL_CFGEDGE_H
#define ISA_MODEL_CFGEDGE_H

#include "isa/model/BB.h"

namespace isa_model {

/** Type of a Control Flow Graph edge. */
typedef enum {
	EDGE_FALLTHROUGH,	/**< Transition BB:n->BB:n+1, no jump. */
	EDGE_CTRLFLOW,		/**< Explicit control flow instruction. */
	EDGE_CPOP_INJECTED	/**< Implicit control flow, injected CPOP. */
} CFGEdge_type;

/** Control Flow Graph (directed) edge. Comes with payload. */
class CFGEdge {
	/** Source of the edge. */
	BB *src;

	/** Destination of this edge. */
	BB *dst;

	/** Number of cycles (pipeline penalty) required to follow this edge. */
	unsigned long cycles;

	/** Type of edge (fall-through, control flow, injected CPOP). */
	CFGEdge_type type;

	/** Number of injected CPOPS required to follow this edge. */
	unsigned int cpops;

	/** State of the CSTACK on this edge. */
	vector<pair<unsigned int, BB *> > cstack;

public:
	/** Constructor
	 * @param s Source BB.
	 * @param d Destination BB.
	 * @param cs Control stack state.
	 * @param j True iff this edge represents a jump (pipeline flush).
	 * @param cp Number of implicit cpops this edge requires.
	 */
	CFGEdge(BB *s, BB *d, vector<pair<unsigned int, BB *> > &cs,
			bool j = false, unsigned int cp = 0);

	/** Return the source BB for this CFG edge.
	 * @return The source BB for this CFG edge. */
	BB *getSrc(void);

	/** Return the destination BB of this CFG edge.
	 * @return The destination BB of this CFG edge. */
	BB *getDst(void);

	/** Set the pipeline penalty paid for following this edge.
	 * @param cyc Number of pipeline cycles paid for following this edge. */
	void setCycles(unsigned long cyc);

	/** Return the pipeline penalty paid for following this edge.
	 * @return Number of cycles. */
	unsigned long getCycles(void) const;

	/** Return true iff this CFGEdge accompanies a PC update.
	 * @return True iff this CFGEdge represents a jump. */
	bool isJump(void);

	/** Return the number of CPOPs required to follow this edge.
	 * @return The number of CPOPs required to follow this edge. */
	unsigned int CPOPCount(void);

	/** Get the type of this edge.
	 * @return Type of this edge. */
	CFGEdge_type getType(void) const;

	/** Return the active CStack state upon following this edge.
	 * @return The active CStack state. */
	vector<pair<unsigned int, BB *> > &getCSTACK(void);

	/** Print the active CStack state along this edge.
	 * @param os Output stream. */
	void printCSTACK(ostream &os);
};

}

#endif /* ISA_MODEL_CFGEDGE_H */
