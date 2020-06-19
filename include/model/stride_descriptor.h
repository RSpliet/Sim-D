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

#ifndef MODEL_STRIDE_DESCRIPTOR_H_
#define MODEL_STRIDE_DESCRIPTOR_H_

#include <systemc>
#include <cstdio>

#include "model/Register.h"
#include "model/workgroup_width.h"
#include "model/request_target.h"
#include "util/constmath.h"

using namespace sc_core;
using namespace sc_dt;
using namespace std;

namespace simd_model {

typedef enum {
	IDX_TRANSFORM_UNIT = 0,
	IDX_TRANSFORM_VEC2 = 1,
	IDX_TRANSFORM_VEC4 = 2
} idx_transform_scheme;

/**
 * Format for a stride memory request descriptor after buffer->physical
 * address translation.
 */
class stride_descriptor {
private:
	/** Destination register */
	AbstractRegister *dst_reg;

public:
	/** Ticket number.
	 *
	 * Used to make the DRAM and SP FIFO's act like one big FIFO under the
	 * "scratchpad as access" scheduling protocol. */
	sc_uint<4> ticket;

	/** Type of request. */
	enum {
		STRIDE,
		IDXIT
	} type;

	/** Start address. */
	sc_uint<32> addr;
	/** Number of words (32-bit) per period. */
	sc_uint<20> words;
	/** Length of period in (32-bit) words. */
	sc_uint<20> period;
	/** Number of periods in this request. */
	sc_uint<20> period_count;

	/** The destination of this request. */
	RequestTarget dst;

	/** Offset to start of data in scratchpad or lane in vector register. */
	sc_uint<32> dst_offset;

	/** Periodicity for the destination SP buffer or register file. */
	sc_uint<20> dst_period;

	/** Destination x-offset when writing to a scratchpad tile. */
	sc_uint<20> dst_off_x;
	/** Destination y-offset when writing to a scratchpad tile. */
	sc_uint<20> dst_off_y;

	/** True iff this is a write operation */
	bool write;

	/** Index transformation, used for 2-vector and 4-vector load/stores. */
	idx_transform_scheme idx_transform;

	/** Default constructor. */
	stride_descriptor();

	/** Constructor for a register-based stride transfer.
	 * @param reg The abstract register base target for this transfer. */
	stride_descriptor(AbstractRegister &reg);

	/** Copy constructor.
	 * @param v Object to copy values from. */
	stride_descriptor(const stride_descriptor &v);

	/** Destructor. */
	~stride_descriptor();

	/** Assignment costructor.
	 * @param v Object to deep-copy values from.
	 * @return Reference to new object &this. */
	inline stride_descriptor &
	operator=(const stride_descriptor &v)
	{
		if (v.dst_reg != nullptr)
			dst_reg = v.dst_reg->clone();
		else
			dst_reg = nullptr;

		type = v.type;
		dst = v.dst;
		addr = v.addr;
		words = v.words;
		period = v.period;
		period_count = v.period_count;
		dst_offset = v.dst_offset;
		dst_period = v.dst_period;
		dst_off_x = v.dst_off_x;
		dst_off_y = v.dst_off_y;
		write = v.write;
		idx_transform = v.idx_transform;
		ticket = v.ticket;

		return *this;
	}

	/** Return the register target type for this descriptor.
	 * @return The destination target type for this descriptor. */
	req_dest_type_t getTargetType(void) const;

	/** Return the target base register for this stride descriptor.
	 *
	 * An AbstractRegister is returned as the vector register column will
	 * be determined separately. The idea was to keep the memory controller
	 * oblivious of the precise SimdCluster configuration (number of
	 * work-items and warps in a work-group). Somewhere down the line this
	 * might not have worked out.
	 * @return The target base register for this transfer.  */
	AbstractRegister *getTargetReg(void) const;

	/** SystemC mandatory print stream operation.
	 * @param os Output stream.
	 * @param v Stride descriptor to print.
	 * @return Output stream. */
	friend ostream &operator<<(ostream &os,
			stride_descriptor const &v);

	/** Construct a stride descriptor from a CSV string.
	 * @param csv String containing a six-tuple of stride descriptor params
	 * @return A stride descriptor, null iff string incorrect */
	static stride_descriptor *from_csv_string(const char *csv);

	/** Construct a stride descriptor from a CSV file opened with fopen().
	 * @param csv File pointer to file containing six-tuples of stride
	 * 	      descriptors
	 * @return A stride descriptor, null iff string incorrect */
	static stride_descriptor *from_csv_file(FILE *csv);


	/** SystemC mandatory trace output.
	 * @param tf Reference to trace file.
	 * @param v reg_offset_t entry to print.
	 * @param NAME Name of the SystemC object.
	 */
	inline friend void
	sc_trace(sc_trace_file *tf, const stride_descriptor &v,
			const std::string & NAME) {
		sc_trace(tf,v.addr, NAME + ".addr");
		sc_trace(tf,v.words, NAME + ".words");
		sc_trace(tf,v.period, NAME + ".period");
		sc_trace(tf,v.period_count, NAME + ".period_count");
		sc_trace(tf,v.dst_offset, NAME + ".dst_offset");
		sc_trace(tf,v.write, NAME + ".write");
	}

	/** Equals operator
	 * @param v Other object to compare against &this
	 * @return True iff the two objects are equal. */
	inline bool
	operator==(const stride_descriptor &v)
	{
		if (type != v.type || dst != v.dst || ticket != v.ticket)
			return false;

		switch (dst.type){
		case TARGET_REG:
		case TARGET_CAM:
			if ((dst_reg == nullptr && v.dst_reg != nullptr) ||
			    (dst_reg != nullptr && v.dst_reg == nullptr))
				return false;

			if (dst_reg && *dst_reg != *v.dst_reg)
				return false;
			break;
		case TARGET_SP:
			break;
		default:
			assert(false);
			break;
		}

		return (addr == v.addr &&
			words == v.words &&
			period == v.period &&
			period_count == v.period_count &&
			dst_offset == v.dst_offset &&
			dst_period == v.dst_period &&
			dst_off_x == v.dst_off_x &&
			dst_off_y == v.dst_off_y &&
			write == v.write &&
			idx_transform == v.idx_transform);
	}
};

}

#endif /* MODEL_STRIDE_DESCRIPTOR_H_ */
