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

#ifndef COMPUTE_MODEL_CTRLSTACK_ENTRY_H
#define COMPUTE_MODEL_CTRLSTACK_ENTRY_H

#include <systemc>

#include "isa/model/Operand.h"

using namespace sc_core;
using namespace sc_dt;
using namespace isa_model;

namespace compute_model {

/** Possible actions on the control stack */
enum ctrlstack_action {
	CTRLSTACK_IDLE,
	CTRLSTACK_POP,
	CTRLSTACK_PUSH
};

/**
 * A single entry on the control stack.
 */
template <unsigned int THREADS = 1024, unsigned int PC_WIDTH = 11>
class ctrlstack_entry {
public:
	/** Predicate mask. */
	sc_bv<THREADS> pred_mask;

	/** Return address */
	sc_uint<PC_WIDTH> pc;

	/** Type of predicate mask */
	sc_uint<2> mask_type;

	/** Default constructor. */
	ctrlstack_entry() : pred_mask(0), pc(0), mask_type(VSP_CTRL_RUN) {}

	/** Fully specified constructor.
	 * @param pm Predicate mask.
	 * @param p Program counter value.
	 * @param mt Predicate mask type. */
	ctrlstack_entry(sc_bv<THREADS> pm, sc_uint<PC_WIDTH> p, sc_uint<2> mt)
	: pred_mask(pm), pc(p), mask_type(mt) {}

	/** SystemC mandatory print stream operation
	 * @param os Output stream.
	 * @param v Object to print.
	 * @return The updated output stream.*/
	inline friend std::ostream&
	operator<<( std::ostream& os,
			ctrlstack_entry<THREADS,PC_WIDTH> const & v )
	{
		os << "ctrlstack_entry(" << std::hex << v.pred_mask << std::dec
				<< ",pc: " << v.pc  << ",type: ";
		switch(v.mask_type) {
		case VSP_CTRL_RUN:
			os << "Control)";
			break;
		case VSP_CTRL_BREAK:
			os << "Break  )";
			break;
		case VSP_CTRL_RET:
			os << "Return )";
			break;
		default:
			os << "Unknown)";
		}

		return os;
	}

	/** SystemC mandatory trace output.
	 * @param tf Reference to trace file.
	 * @param v Control stack entry to print.
	 * @param NAME Name of the SystemC object.
	 */
	inline friend void
	sc_trace(sc_trace_file *tf, const ctrlstack_entry<THREADS,PC_WIDTH> & v,
	    const std::string & NAME ) {
	      sc_trace(tf,v.pred_mask, NAME + ".pred_mask");
	      sc_trace(tf,v.pc, NAME + ".pc");
	      sc_trace(tf,v.mask_type, NAME + ".mask_type");
	    }

	/** Comparator.
	 * @param v Object to compare to this.
	 * @return true iff objects are equal.
	 */
	inline bool
	operator==(const ctrlstack_entry<THREADS,PC_WIDTH> & v) const {
		return (pred_mask == v.pred_mask && pc == v.pc &&
				mask_type == v.mask_type);
	}

	/** Copy constructor.
	 * @param v Object to copy data from into this object.
	 * @return New object with same data as v.
	 */
	inline ctrlstack_entry<THREADS,PC_WIDTH>& operator=
			(const ctrlstack_entry<THREADS,PC_WIDTH> & v) {
		pred_mask = v.pred_mask;
		pc = v.pc;
		mask_type = v.mask_type;
		return *this;
	}
};

}

#endif /* COMPUTE_MODEL_CTRLSTACK_ENTRY_H */
