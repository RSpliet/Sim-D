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

#ifndef INCLUDE_MODEL_REQUEST_TARGET_H
#define INCLUDE_MODEL_REQUEST_TARGET_H

#include <systemc>

#include "util/constmath.h"

using namespace sc_core;
using namespace sc_dt;
using namespace std;

namespace simd_model {

typedef enum {
	TARGET_NONE = -1,
	TARGET_SP = 0,
	TARGET_REG = 1,
	TARGET_CAM = 2,
} req_dest_type_t;

typedef enum {
	IF_SP_WG0 = 0,
	IF_SP_WG1 = 1,
	IF_RF = 2,
	IF_DRAM = 2,
	IF_SENTINEL = 3,
} req_if_t;

/** Data class for the DRAM/SP controller to inform the register file which
 * register file/type is targeted by a transfer. */
class RequestTarget
{
public:
	/** @todo To support multiple SimD clusters, add the cluster ID */
	sc_uint<1> wg; /**< Work-group. */
	req_dest_type_t type; /**< Destination type. */

	/** Default empty contstructor. */
	RequestTarget() : wg(0), type(TARGET_NONE) {}

	/** Constructor
	 * @param w Work-group slot to target (0,1).
	 * @param d Destination type. */
	RequestTarget(sc_uint<1> w, req_dest_type_t d)
	: wg(w), type(d) {}

	/** Return the SimdCluster interface for this register
	 * type/work-group.
	 * @return the SimdCluster interface index for this transfer.*/
	req_if_t
	get_interface(void)
	{
		if (type == TARGET_REG || type == TARGET_CAM)
			return IF_RF;

		return wg == 0 ? IF_SP_WG0 : IF_SP_WG1;
	}

	/** Stream print operator */
	inline friend ostream &
	operator<<(ostream &os, RequestTarget const &v)
	{
		os << v.wg << "," << int(v.type);

		return os;
	}

	/** Equals.
	 * @param v Other object to compare against.
	 * @return True iff the two objects are equal. */
	inline bool
	operator==(RequestTarget const &v)
	{
		return wg == v.wg && type == v.type;
	}

	/** Not equals.
	 * @param v Other object to compare against.
	 * @return False iff the two objects are equal. */
	inline bool
	operator!=(RequestTarget const &v)
	{
		return !(*this == v);
	}

	/** Copy constructor, assignment operator.
	 * @param v Other object to take values from.
	 * @return Reference to &this with the new values set. */
	inline RequestTarget &
	operator=(RequestTarget const &v)
	{
		wg = v.wg;
		type = v.type;
		return *this;
	}

	/** SystemC mandatory trace output.
	 * @param tf Reference to trace file.
	 * @param v reg_offset_t entry to print.
	 * @param NAME Name of the SystemC object.
	 */
	inline friend void
	sc_trace(sc_trace_file *tf, const RequestTarget &v,
			const std::string & NAME) {
		sc_trace(tf,v.wg, NAME + ".wg");
		sc_trace(tf,v.type, NAME + ".type");
	}
};

/** Data class used to convey either a target vector register lane/row or an
 * offset within a buffer.
 *
 * The main use of this data class is to provide an interface for the
 * DRAM/scratchpad controller to steer data on it's output data line to the
 * correct location(s) in the VRF/VSP.
 * @param THREADS Number of work-items in a work-group.
 *  */
template <unsigned int THREADS = 1024>
union reg_offset_t {
	/** Offset within a vector register (file) */
	struct {
		unsigned int lane : const_log2(THREADS); /**< VRF column. */
		unsigned int row : 30 - const_log2(THREADS); /**< VRF row. */
	};
	/** Offset within a DRAM/scratchpad buffer. */
	unsigned int idx : 30;

	/** Default/empty constructor. */
	reg_offset_t() : idx(0) {}

	/** Constructor for reg_offset_t as a buffer offset.
	 * @param i Index, buffer offset. */
	reg_offset_t(unsigned int i) : idx(i) {}

	/** Constructor for reg_offset_t as a vector register offset.
	 * @param l Lane, column in the vector register file.
	 * @param r Row, the vector register to place this result in (used for
	 * 	    e.g. 2-vector/4-vector load/store).*/
	reg_offset_t(unsigned int l, unsigned int r) : lane(l), row(r) {}

	/** Stream output for debug info.
	 * @param os Output stream,
	 * @param v Register/offset object to print,
	 * @return Output stream. */
	inline friend ostream &
	operator<<(ostream &os, reg_offset_t<THREADS> const &v)
	{
		os << v.idx;

		return os;
	}

	/** Equals comparator for reg_offset_t object.
	 * @param v Object to compare &this against.
	 * @return True iff the two objects are equal. */
	inline bool
	operator==(reg_offset_t<THREADS> const &v)
	{
		return idx == v.idx;
	}

	/** Assignment operator for reg_offset_t object.
	 * @param v Object to take deep-copy values from.
	 * @return The new object. */
	inline reg_offset_t<THREADS> &
	operator=(reg_offset_t<THREADS> const &v)
	{
		idx = v.idx;
		return *this;
	}

	/** SystemC mandatory trace output.
	 * @param tf Reference to trace file.
	 * @param v reg_offset_t entry to print.
	 * @param NAME Name of the SystemC object.
	 */
	inline friend void
	sc_trace(sc_trace_file *tf, const reg_offset_t<THREADS> &v,
			const std::string &NAME) {
		sc_trace(tf,v.idx, NAME + ".idx");
		sc_trace(tf,v.row, NAME + ".row");
		sc_trace(tf,v.lane, NAME + ".lane");
	}
};

/** Data class to convey a DRAM buffer offset to vc.mem_data column mapping to
 * the IndexIterator.
 * @param THREADS Number of work-items in a work-group.
 */
template <unsigned int THREADS = 1024>
class idx_t {
public:
	/** True iff this index is the last to be transmitted. */
	bool dummy_last;

	/** "Column" in the CAM vector register. */
	sc_uint<const_log2(THREADS)> cam_idx;

	/** The offset within the buffer */
	sc_uint<30> dram_off;

	/** Default/empty constructor. */
	idx_t() : dummy_last(true), cam_idx(0), dram_off(0) {}

	/** Constructor
	 * @param i Column in the CAM vector register,
	 * @param o Offset within the DRAM buffer, in number of 32-bit words. */
	idx_t(unsigned int i, unsigned int o)
	: dummy_last(false), cam_idx(i), dram_off(o) {}

	/** Stream output for debug info.
	 * @param os Output stream,
	 * @param v Index object to print,
	 * @return Output stream. */
	inline friend ostream &
	operator<<(ostream &os, idx_t<THREADS> const &v)
	{
		os << v.cam_idx << "(" << hex << v.dram_off << dec << ")";

		return os;
	}

	/** Equals comparator for idx_t object.
	 * @param v Object to compare &this against.
	 * @return True iff the two objects are equal. */
	inline bool
	operator==(idx_t<THREADS> const &v)
	{
		if (dummy_last != v.dummy_last)
			return false;

		return dummy_last ||
			(cam_idx == v.cam_idx && dram_off == v.dram_off);
	}

	/** Assignment operator for idx_t object.
	 * @param v Object to take deep-copy values from.
	 * @return The new object. */
	inline idx_t<THREADS> &
	operator=(idx_t<THREADS> const &v)
	{
		dummy_last = v.dummy_last;
		cam_idx = v.cam_idx;
		dram_off = v.dram_off;
		return *this;
	}

	/** SystemC mandatory trace output.
	 * @param tf Reference to trace file.
	 * @param v reg_offset_t entry to print.
	 * @param NAME Name of the SystemC object.
	 */
	inline friend void
	sc_trace(sc_trace_file *tf, const idx_t<THREADS> &v,
			const std::string &NAME) {
		sc_trace(tf,v.dummy_last, NAME + ".dummy_last");
		sc_trace(tf,v.cam_idx, NAME + ".cam_idx");
		sc_trace(tf,v.dram_off, NAME + ".dram_off");
	}
};

}

#endif /* INCLUDE_MODEL_REQUEST_TARGET_H */
