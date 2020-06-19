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

#ifndef COMPUTE_MODEL_REGISTER_H
#define COMPUTE_MODEL_REGISTER_H

#include <systemc>

#include "model/Register.h"
#include "util/constmath.h"

using namespace sc_core;
using namespace sc_dt;
using namespace std;
using namespace simd_model;

namespace compute_model {
/** A register read request.
 *
 * Can request up to three operands.
 * @param COLS Number of possible columns for vector registers. */
template <unsigned int COLS = 8>
class reg_read_req {
public:
	/** The registers to read */
	Register<COLS> reg[3];

	/** Which read ports are enabled this cycle. */
	sc_bv<3> r;

	/** Immediate value to broadcast.
	 * @todo I don't think it makes sense encoding more than one
	 * immediate in an instruction. Perhaps reduce # wires? */
	sc_uint<32> imm[3];

	/** SystemC mandatory print stream operation.
	 * @param os Stream output.
	 * @param v reg_read_req to print. */
	inline friend std::ostream&
	operator<<(std::ostream& os, reg_read_req<COLS> const &v)
	{
		os << "reg_read_req(";
		if (v.r[0])
			os << v.reg[0] << ",";
		if (v.r[1])
			os << v.reg[1] << ",";
		if (v.r[2])
			os << v.reg[2] << ",";

		os << ")";
		return os;
	}
};

}

#endif /* COMPUTE_MODEL_REGISTER_H */
