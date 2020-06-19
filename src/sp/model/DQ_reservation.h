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

#ifndef SP_MODEL_DQ_RESERVATION_H
#define SP_MODEL_DQ_RESERVATION_H

#include <systemc>
#include <array>

#include "model/request_target.h"

using namespace sc_core;
using namespace sc_dt;
using namespace std;
using namespace simd_model;

namespace sp_model {

/** Pipeline stage (output signals) from DQ to the Register File.
 * @param BUS_WIDTH Number of (32-bit) words outputted per cycle.
 * @param THREADS Number of threads in a work-group. */
template <unsigned int BUS_WIDTH, unsigned int THREADS>
class dq_pipe_rf
{
public:
	bool rw; /**< Enable (read/write) bit. */
	bool write_w; /**< True iff this operation is a write operation. */
	sc_bv<BUS_WIDTH> mask_w; /**< Write mask for write operations. */

	/** Indexes in RF corresponding with each word from/to SP. */
	reg_offset_t<THREADS> idx_w[BUS_WIDTH];

	/** True iff this DQ reservation is the last of the request. */
	bool last;

	/** Empty constructor of invalid (NOP) dq_pipe_rf object. */
	dq_pipe_rf() : rw(false), write_w(false), mask_w(0), last(false) {}

	/** Constructor.
	 * @param m Write mask.
	 * @param en Enable bit
	 * @param w Write bit.
	 * @param l Boolean indicating this entry is the last.*/
	dq_pipe_rf(sc_bv<BUS_WIDTH> m, bool en, bool w, bool l)
	: rw(en), write_w(w), mask_w(m), last(l) {}

	/** SystemC mandatory print stream operation */
	inline friend ostream &
	operator<<(ostream &os, dq_pipe_rf<BUS_WIDTH,THREADS> const &v)
	{
		os << "@"<< " pipe_rf(" << (v.rw ? "Enabled " : "Disabled ") <<
				(v.write_w ? "W" : "R") << v.mask_w << ")";

		return os;
	}
};

/** Pipeline stage (output signals) from DQ to the StorageArray.
 * @param BUS_WIDTH Number of (32-bit) words outputted per cycle. */
template <unsigned int BUS_WIDTH>
class dq_pipe_sa
{
public:
	bool rw; /**< Enable (read/write) bit. */
	bool write_w; /**< True iff this operation is a write operation. */
	sc_bv<BUS_WIDTH> mask_w; /**< Write mask for write operations. */
	sc_uint<18> addr; /**< Scratchpad address of first word. */
	bool last; /**< Boolean indicating this reservation is the last for the
		    *   current request. */

	/** Empty constructor of invalid (NOP) dq_pipe_sa object. */
	dq_pipe_sa()
	: rw(false), write_w(false), mask_w(0), addr(0), last(false) {}

	/** Constructor.
	 * @param m Write mask.
	 * @param en Enable bit
	 * @param a Scratchpad address.
	 * @param w Write bit.
	 * @param l Boolean indicating this DQ reservation is the last of the
	 * 	    request. */
	dq_pipe_sa(sc_bv<BUS_WIDTH> m, bool en, sc_uint<18> a, bool w, bool l)
	: rw(en), write_w(w), mask_w(m), addr(a), last(l) {}

	/** SystemC mandatory print stream operation */
	inline friend ostream &
	operator<<(ostream &os, dq_pipe_sa<BUS_WIDTH> const &v)
	{
		os << "@"<< " pipe_sa(" << hex << v.addr << dec << " " <<
				(v.write_w ? "W" : "R") << v.mask_w << ")";

		return os;
	}

	/** Comparator.
	 * @param v Object to compare to this.
	 * @return true iff objects are equal.
	 */
	inline bool
	operator==(const dq_pipe_sa<BUS_WIDTH> & v) const {
		return (rw == v.rw && mask_w == v.mask_w &&
			addr == v.addr && write_w == v.write_w);
	}

	/** SystemC mandatory trace output.
	 * @param tf Reference to trace file.
	 * @param v StorageArray pipe entry to print.
	 * @param NAME Name of the SystemC object.
	 */
	inline friend void
	sc_trace(sc_trace_file *tf, const dq_pipe_sa<BUS_WIDTH> &v,
	    const std::string & NAME ) {
		sc_trace(tf,v.rw, NAME + ".rw");
		sc_trace(tf,v.mask_w, NAME + ".mask_w");
		sc_trace(tf,v.addr, NAME + ".addr");
		sc_trace(tf,v.write_w, NAME + ".write_w");
	}
};

/**
 * Struct stores a single reservation on the data bus.
 */
template <unsigned int BUS_WIDTH, unsigned int THREADS>
class DQ_reservation
{
public:
	/** Read/write enable bit. If false, reservation is invalid (NOP). */
	bool rw;

	/** Mask of words to read/write */
	sc_bv<BUS_WIDTH> wordmask;

	/** Offset in scratchpad where this data must be stored/loaded from */
	sc_uint<18> sp_offset;

	/** Read/write targets register file or scratchpad? */
	req_dest_type_t target;

	/** Index into register file for each word. */
	reg_offset_t<THREADS> reg_offset[BUS_WIDTH];

	/** True iff this operation is a write (RF -> SP) operation. */
	bool write;

	/** Indicate the end of this stride r/w */
	bool last;

	/** Empty (invalid) constructor. */
	DQ_reservation()
	: rw(false), wordmask(0), sp_offset(0), target(TARGET_REG),
	  write(false), last(false) {}

	/** Constructor for given target register indexes (mainly for CAM
	 * consumption). */
	DQ_reservation(sc_uint<18> a, sc_bv<BUS_WIDTH> m, bool w,
			req_dest_type_t t, array<reg_offset_t<THREADS>,BUS_WIDTH> &ri)
	: rw(true), sp_offset(a), target(t), write(w), last(false)
	{
		unsigned int i;
		unsigned int j;

		j = (sp_offset >> 2) & (BUS_WIDTH - 1);
		for (i = 0; i < BUS_WIDTH; i++, j = (j + 1) % BUS_WIDTH) {
			reg_offset[j] = ri[i];
			wordmask[j] = m[i];
		}
	}

	/** Constructor for given target register indexes (mainly for CAM
	 * consumption). */
	DQ_reservation(sc_uint<18> a, sc_bv<BUS_WIDTH> m, bool w,
			req_dest_type_t t, array<unsigned int,BUS_WIDTH> ri)
	: rw(true), sp_offset(a), target(t), write(w), last(false)
	{
		unsigned int i;
		unsigned int j;

		j = (sp_offset >> 2) & (BUS_WIDTH - 1);
		for (i = 0; i < BUS_WIDTH; i++, j = (j + 1) % BUS_WIDTH) {
			reg_offset[j].idx = ri[i];
			wordmask[j] = m[i];
		}
	}

	/** Constructor for given target register row/columns. */
	DQ_reservation(sc_uint<18> a, sc_bv<BUS_WIDTH> m, bool w,
			req_dest_type_t t, array<unsigned int,BUS_WIDTH> ri,
			array<unsigned int,BUS_WIDTH> rr)
	: rw(true), sp_offset(a), target(t), write(w), last(false)
	{
		unsigned int i;
		unsigned int j;

		j = (sp_offset >> 2) & (BUS_WIDTH - 1);
		for (i = 0; i < BUS_WIDTH; i++, j = (j + 1) % BUS_WIDTH) {
			reg_offset[j].lane = ri[i];
			reg_offset[j].row = rr[i];
			wordmask[j] = m[i];
		}
	}

	/** Return dq_pipe_rf structure from this reservation.
	 * @return dq_pipe_rf structure with RF signals. */
	dq_pipe_rf<BUS_WIDTH,THREADS>
	get_pipe_rf(void)
	{
		dq_pipe_rf<BUS_WIDTH,THREADS> prf(wordmask, rw, write, last);
		unsigned int i;

		for (i = 0; i < BUS_WIDTH; i++)
			prf.idx_w[i] = reg_offset[i];

		return prf;
	}

	/** Return dq_pipe_sa structure from this reservation.
	 * @return dq_pipe_sa object with StorageArray signals. */
	dq_pipe_sa<BUS_WIDTH>
	get_pipe_sa(void)
	{
		return dq_pipe_sa<BUS_WIDTH>(wordmask, rw, sp_offset, write,
				last);
	}

	/** SystemC mandatory print stream operation */
	inline friend ostream &
	operator<<(ostream &os, DQ_reservation<BUS_WIDTH,THREADS> const &v)
	{
		unsigned int i;

		os << "@"<< " DQ(SP(" << hex << v.sp_offset << dec << ") " <<
				(v.write ? "-> " : "<- ") <<
				(v.target == TARGET_REG ? "REG " : "CAM ") <<
				v.wordmask << ") [";

		for (i = 0; i < BUS_WIDTH; i++)
			os << v.reg_offset[i] << ",";
		os << "]";

		return os;
	}

	/** SystemC mandatory trace function. */
	inline friend void sc_trace(sc_trace_file *tf,
		const DQ_reservation<BUS_WIDTH,THREADS> &v, const string &NAME)
	{
		sc_trace(tf,v.rw, NAME + ".rw");
		sc_trace(tf,v.wordmask, NAME + ".wordmask");
		sc_trace(tf,v.sp_offset, NAME + ".sp_offset");
		sc_trace(tf,v.target, NAME + ".target");
		sc_trace(tf,v.write, NAME + ".write");
		sc_trace(tf,v.last, NAME + ".last");
	}

	/** Comparator. */
	inline bool
	operator==(DQ_reservation<BUS_WIDTH,THREADS> const &v)
	{
		unsigned int i;

		if (target != v.target)
			return false;

		switch (target) {
		case TARGET_CAM:
			if (wordmask != v.wordmask)
				return false;

			for (i = 0; i < BUS_WIDTH; i++) {
				if (wordmask[i] &&
				    reg_offset[i].idx != v.reg_offset[i].idx)
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

		return (rw == v.rw && sp_offset == v.sp_offset &&
			write == v.write && last == v.last);
	}

	/** Copy constructor.
	 * @param v Object to copy data from
	 * @return A reference to the newly created DQ_reservation object,
	 * containing the same data as v.
	 */
	inline DQ_reservation<BUS_WIDTH,THREADS> &
	operator=(const DQ_reservation<BUS_WIDTH,THREADS> &v)
	{
		unsigned int i;

		rw = v.rw;
		wordmask = v.wordmask;
		sp_offset = v.sp_offset;
		target = v.target;
		write = v.write;
		last = v.last;
		for (i = 0; i < BUS_WIDTH; i++)
			reg_offset[i] = v.reg_offset[i];

		return *this;
	}
};

}

#endif /* SP_MODEL_DQ_RESERVATION_H_*/
