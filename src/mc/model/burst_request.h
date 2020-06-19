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

#ifndef MC_MODEL_BURST_REQUEST_H
#define MC_MODEL_BURST_REQUEST_H

#include <systemc>
#include <array>

#include "model/request_target.h"

using namespace sc_core;
using namespace sc_dt;
using namespace simd_model;
using namespace std;

namespace mc_model {

/** Precharge policies */
enum PrechargePolicy {
	/** Linear, optimised for monotonically increasing DRAM addresses. */
	PRECHARGE_LINEAR,
	/** As Late As Possible precharge policy, best for random addresses. */
	PRECHARGE_ALAP
};

/** Request for a single burst */
template <unsigned int BUS_WIDTH, unsigned int THREADS>
class burst_request {
public:
	/** Start address. */
	sc_uint<32> addr;

	/** Address of next request */
	sc_uint<32> addr_next;

	/** Bit-vector of word masks - true if the word at pos n should be
	 * read/written. */
	sc_bv<BUS_WIDTH> wordmask;

	/** True iff this operation is a write op. */
	bool write;

	/** Hint to the CmdGen what precharge policy to apply */
	PrechargePolicy pre_pol;

	/** Destination is a register or SP? */
	RequestTarget target;

	/** Offset to start of data in scratchpad */
	sc_uint<32> sp_offset;

	/** Index into register file for each word. */
	reg_offset_t<THREADS> reg_offset[BUS_WIDTH];

	/** This is the last burst request resulting from a stride or set of
	 * indexes. */
	bool last;

	/** Default constructor. */
	burst_request()
	: addr(0), wordmask(0), write(0), pre_pol(PRECHARGE_LINEAR),
	  target(0,TARGET_NONE), sp_offset(0), last(false) {}

	/** Burst request constructor for scratchpad destination.
	 *
	 * Its primary user is the various unit tests.
	 * @param a DRAM address
	 * @param wm Word-mask to read
	 * @param w True iff this burst request is a write request, false for
	 * 	    read.
	 * @param wg Destination work-group slot.
	 * @param sp Scratchpad destination address. */
	burst_request(sc_uint<32> a, sc_bv<BUS_WIDTH> wm, bool w, sc_uint<1> wg,
			sc_uint<32> sp)
	: addr(a), wordmask(wm), write(w), pre_pol(PRECHARGE_LINEAR),
	  target(wg,TARGET_SP), sp_offset(sp), last(false) {}

	/** Burst request constructor for vector register or scratchpad
	 * destination.
	 *
	 * Its primary user is the various unit tests.
	 * @param a DRAM address
	 * @param wm Word-mask to read
	 * @param w True iff this burst request is a write request, false for
	 * 	    read.
	 * @param wg Destination work-group slot.
	 * @param tg Type of the destination register.
	 * @param ri Array of vector register columns. */
	burst_request(sc_uint<32> a, sc_bv<BUS_WIDTH> wm, bool w, sc_uint<1> wg,
			req_dest_type_t tg, array<unsigned int,BUS_WIDTH> ri)
	: addr(a), wordmask(wm), write(w), pre_pol(PRECHARGE_LINEAR),
	  target(wg,tg), sp_offset(0), last(false)
	{
		unsigned int i;

		if (target.type == TARGET_SP) {
			sp_offset = ri[0];
		} else {
			for (i = 0; i < BUS_WIDTH; i++)
				reg_offset[i].idx = ri[i];
		}
	}

	/** Burst request constructor for vector register destination,
	 * supporting multiple consecutive destination vector registers.
	 *
	 * Its primary user is the various unit tests testing 2-vector and
	 * 4-vector load/stores.
	 * @param a DRAM address
	 * @param wm Word-mask to read
	 * @param w True iff this burst request is a write request, false for
	 * 	    read.
	 * @param wg Destination work-group slot.
	 * @param tg Type of the destination register.
	 * @param ri Array of vector register lanes (columns).
	 * @param rr Array of vector register rows. */
	burst_request(sc_uint<32> a, sc_bv<BUS_WIDTH> wm, bool w, sc_uint<1> wg,
			req_dest_type_t tg, array<unsigned int,BUS_WIDTH> ri,
			array<unsigned int,BUS_WIDTH> rr)
	: addr(a), wordmask(wm), write(w), pre_pol(PRECHARGE_LINEAR),
	  target(wg,tg), sp_offset(0), last(false)
	{
		unsigned int i;

		if (target.type == TARGET_SP) {
			sp_offset = ri[0];
		} else {
			for (i = 0; i < BUS_WIDTH; i++) {
				reg_offset[i].lane = ri[i];
				reg_offset[i].row = rr[i];
			}
		}

	}

	/** SystemC mandatory print stream operation.
	 * @param os Output stream
	 * @param v Burst request to print
	 * @return Output stream */
	inline friend std::ostream&
	operator<<(std::ostream &os, burst_request<BUS_WIDTH,THREADS> const &v)
	{
		unsigned int i;

		os << "burst_req(" << std::hex << v.addr << ",next: "
				<< v.addr_next << std::dec << ","
				<< v.wordmask << ") "
				<< (v.write ? "<-" : "->");

		switch (v.target.type) {
		case TARGET_SP:
			os << " SP " << std::hex << v.sp_offset << std::dec;
			break;
		case TARGET_REG:
			os << " vreg [";

			for (i = 0; i < BUS_WIDTH; i++) {
				if (i > 0)
					os << ",";

				if (v.wordmask[i])
					os << "(" << v.reg_offset[i].lane << ","
						<< v.reg_offset[i].row << ")";
				else
					os << "-";
			}
			os << "]";
			break;
		case TARGET_CAM:
			os << " vreg [";

			for (i = 0; i < BUS_WIDTH; i++) {
				if (i > 0)
					os << ",";

				if (v.wordmask[i])
					os << v.reg_offset[i].idx;
				else
					os << "-";
			}
			os << "]";
			break;
		default:
			break;
		}
		return os;
	}

	/** Equals operator
	 * @param v Object to compare &this against.
	 * @return True iff the two objects are equal. */
	inline bool
	operator==(burst_request<BUS_WIDTH,THREADS> const &v)
	{
		unsigned int i;

		if (target != v.target)
			return false;

		switch (target.type) {
		case TARGET_SP:
			if (sp_offset != v.sp_offset)
				return false;
			break;
		case TARGET_CAM:
			if (wordmask != v.wordmask)
				return false;

			for (i = 0; i < BUS_WIDTH; i++) {
				if (wordmask[i] && reg_offset[i].idx != v.reg_offset[i].idx)
					return false;
			}
			break;
		case TARGET_REG:
			if (wordmask != v.wordmask)
				return false;

			for (i = 0; i < BUS_WIDTH; i++) {
				if (wordmask[i] &&
				    (reg_offset[i].row != v.reg_offset[i].row ||
				     reg_offset[i].lane != v.reg_offset[i].lane))
					return false;
			}
			break;
		default:
			break;
		}

		return (addr == v.addr && addr_next == v.addr_next &&
				wordmask == v.wordmask && write == v.write &&
				pre_pol == v.pre_pol &&
				last == v.last);
	}
};

}

#endif /* MC_MODEL_BURST_REQUEST_H */
