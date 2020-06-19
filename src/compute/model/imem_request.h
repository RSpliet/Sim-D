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

#ifndef COMPUTE_MODEL_IMEM_REQUEST_H
#define COMPUTE_MODEL_IMEM_REQUEST_H

using namespace sc_core;
using namespace sc_dt;
using namespace std;

namespace compute_model {

/** Request for an instruction memory entry. */
template <unsigned int PC_WIDTH>
class imem_request {
public:
	/** PC of instruction */
	sc_uint<PC_WIDTH> pc;

	/** Valid? For false, output "NOP" */
	bool valid;

	/** SystemC mandatory print stream operation
	 * @param os Output stream.
	 * @param v Object to print.
	 * @return The updated output stream.*/
	inline friend std::ostream&
	operator<<( std::ostream& os,
			imem_request<PC_WIDTH> const & v )
	{
		os << "imem_request(pc: " << v.pc << ")";

		return os;
	}
};

}

#endif /* COMPUTE_MODEL_IMEM_REQUEST_H */
