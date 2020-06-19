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

#ifndef MC_MODEL_DQ_RESERVATION_H
#define MC_MODEL_DQ_RESERVATION_H

#include <systemc>

#include "util/constmath.h"
#include "model/request_target.h"

using namespace sc_core;
using namespace sc_dt;
using namespace std;
using namespace simd_model;

namespace mc_model {
/**
 * Struct stores a single reservation on the data bus.
 */
template <unsigned int BUS_WIDTH, unsigned int DRAM_BANKS,
	  unsigned int THREADS>
class DQ_reservation
{
public:
	/** Time of arrival of first two burst beats */
	long cycle;

	/** Mask of words to read/write */
	sc_bv<BUS_WIDTH> wordmask;

	/** DRAM row for read-write operation. Superfluous in a real DRAM
	 * controller (just like col and bank) because cmd_DDR already
	 * communicated this, but for simulation purposes good to have around.*/
	sc_uint<32> row;
	/** DRAM column */
	sc_uint<20> col;
	/** DRAM bank */
	sc_uint<const_log2(DRAM_BANKS)> bank;

	/** Read/write targets register file or scratchpad? */
	RequestTarget target;

	/** Index into register file for each word. */
	reg_offset_t<THREADS> reg_offset[BUS_WIDTH];

	/** True iff this operation is a write operation */
	bool write;

	/** Offset in scratchpad where this data must be stored/loaded from */
	sc_uint<18> sp_offset;

	/** SystemC mandatory print stream operation */
	inline friend ostream &
	operator<<(ostream &os, DQ_reservation const &v)
	{
		os << "@"<< v.cycle << " DQ(" << hex << v.row << ", "
				<< v.col << ", " << v.bank << " " << dec
				<< "-> SP(" << hex << v.sp_offset << dec << " " <<
				(v.write ? "W " : "R ") << v.wordmask << ")";

		return os;
	}
};

}

#endif /* MC_MODEL_DQ_RESERVATION_H_*/
