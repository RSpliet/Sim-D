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

#ifndef MC_MODEL_CMD_DDR_H_
#define MC_MODEL_CMD_DDR_H_

#include <systemc>

#include "model/request_target.h"

using namespace sc_core;
using namespace sc_dt;
using namespace std;
using namespace simd_model;

namespace mc_model {

/**
 * Format for a memory request after buffer->physical address translation.
 * A lot of these structs could be replaced with ramulator concepts - but I'd
 * rather keep the interface between ramulator and Sim-D contained within
 * CmdArb_DDRx */
template <unsigned int BUS_WIDTH, unsigned int THREADS>
class cmd_DDR {
public:
	/** DRAM row */
	sc_uint<32> row;
	/** DRAM column */
	sc_uint<20> col;
	/** Precharge prior to activate */
	bool pre_pre;
	/** Activate this row prior to executing op */
	bool act;
	/** Operation is a read op */
	bool read;
	/** Operation is a write operation */
	bool write;
	/** (Auto-)precharge after read/write */
	bool pre_post;
	/** Word-mask, propagated from burst_request */
	sc_bv<BUS_WIDTH> wordmask;
	/** Offset to start of data in scratchpad */
	sc_uint<32> sp_offset;

	/** The request target */
	RequestTarget target;

	/** Index into register file for each word. */
	reg_offset_t<THREADS> reg_offset[BUS_WIDTH];

	/** SystemC mandatory print stream operation */
	inline friend ostream&
	operator<<( ostream& os, cmd_DDR const & v )
	{
		unsigned int i;

		os << "RWP(" << v.row << "," << v.col << ":" <<
				(v.pre_pre ? "p" : "-") <<
				(v.act ? "A" : "-") << (v.read ? "R" : "-")
				<< (v.write ? "W" : "-")
				<< (v.pre_post ? "P" : "-") << ")";

		if (v.target.type == TARGET_SP) {
			os << "-> SP " << std::hex << v.sp_offset << std::dec;
		} else {
			os << "-> REG [";
			for (i = 0; i < BUS_WIDTH; i++) {
				if (v.wordmask[i]) {
					if (v.target.type == TARGET_CAM)
						os << v.reg_offset[i].idx << ",";
					else
						os << "(" << v.reg_offset[i].lane
							<< "," << v.reg_offset[i].row
							<< "),";
				} else {
					os << "-,";
				}
			}
		}

		os << "] \t# " << v.wordmask;

		return os;
	}
};

}

#endif /* MC_MODEL_CMD_DDR_H_ */
