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

#ifndef COMPUTE_MODEL_WORK_H
#define COMPUTE_MODEL_WORK_H

#include <systemc>
#include <vector>
#include <array>

#include "util/constmath.h"
#include "util/sched_opts.h"
#include "isa/model/Program.h"
#include "isa/model/Instruction.h"
#include "model/workgroup_width.h"

using namespace sc_core;
using namespace sc_dt;
using namespace std;
using namespace simd_model;
using namespace isa_model;

namespace compute_model {

/** State of a work-group. */
typedef enum {
	WG_STATE_NONE = 0,
	WG_STATE_RUN,
	WG_STATE_BLOCKED_DRAM,
	WG_STATE_BLOCKED_DRAM_POSTEXIT,
	WG_STATE_BLOCKED_SP,
	WG_STATE_SENTINEL
} workgroup_state;

/** Kernel invocation request. */
template <unsigned int XLAT_ENTRIES = 32>
class work {
public:
	/** X,Y dimensions for this kernel invocation */
	unsigned int dims[2];
	
	/** Width (X-dimension) of a single workgroup.
	 * Height = THREADS / width */
	workgroup_width wg_width;

	/** Instructions for kernel program */
	vector<Instruction> imem;

	/** Buffer mappings. */
	array<Buffer, XLAT_ENTRIES> buf_map;

	/** Number of mappings. */
	unsigned int bufs;

	/** Buffer mappings for scratchpad. */
	array<Buffer, XLAT_ENTRIES> sp_buf_map;

	/** Number of scratchpad buffers. */
	unsigned int sp_bufs;

	/** Scheduling policy. */
	sc_bv<WSS_SENTINEL> ws_sched;

	/** Empty constructor. */
	work() : wg_width(WG_WIDTH_32), bufs(0), sp_bufs(0), ws_sched(0) {
		dims[0] = 0;
		dims[1] = 0;
	}

	/** Constructor.
	 * @param x Number of threads in X dimension.
	 * @param y Number of threads in Y dimension, 0 for 1D.
	 * @param w Width of workgroups. */
	work(unsigned int x, unsigned int y, workgroup_width w) : wg_width(w),
	bufs(0), sp_bufs(0), ws_sched(0)
	{
		dims[0] = x;
		dims[1] = y;
	}

	/** Add an instruction to the kernel.
	 * @param op Instruction to add. */
	inline void
	add_op(Instruction op)
	{
		imem.push_back(op);
	}

	/** Add a DRAM buffer to the kernel specification.
	 * @param buf Buffer to add to kernel. */
	inline void
	add_buf(Buffer buf)
	{
		if (bufs == XLAT_ENTRIES - 1)
			return;

		buf_map[bufs++] = buf;
	}

	/** Add a DRAM buffer to the kernel specification.
	 * @param buf Buffer to add to kernel. */
	inline void
	add_sp_buf(Buffer buf)
	{
		if (sp_bufs == XLAT_ENTRIES - 1)
			return;

		sp_buf_map[sp_bufs++] = buf;
	}

	/** Obtain the buffer specification at index provided.
	 * @param idx Index of the requested buffer. */
	inline sc_uint<32>
	get_buf(unsigned int idx)
	{
		if (idx < XLAT_ENTRIES)
			return buf_map[idx].getAddress();

		return 0xdead0000;
	}

	/** Obtain the buffer specification at index provided.
	 * @param idx Index of the requested buffer. */
	inline sc_uint<32>
	get_sp_buf(unsigned int idx)
	{
		if (idx < XLAT_ENTRIES)
			return sp_buf_map[idx].getAddress();

		return 0xdead0000;
	}

	/** Return the number of mapped DRAM buffers.
	 * @return The number of active buffers.*/
	unsigned int
	get_bufs(void) const
	{
		return bufs;
	}

	/** Return the number of mapped DRAM buffers.
	 * @return The number of active buffers.*/
	unsigned int
	get_sp_bufs(void) const
	{
		return sp_bufs;
	}

	/** Set scheduling options.
	 * @param s Mask of scheduling options to be enabled. */
	void
	set_sched_options(sc_bv<WSS_SENTINEL> s)
	{
		ws_sched = s;
	}

	/** @todo Buffer mapping. Code always bound to buffer 0? */
	/** SystemC mandatory print stream operation */
	inline friend std::ostream&
	operator<<(std::ostream& os, work const &v)
	{
		os << "work(" << ")";
		return os;
	}

	/** SystemC mandatory trace output.
	 * @param tf Reference to trace file.
	 * @param v Control stack entry to print.
	 * @param NAME Name of the SystemC object.
	 */
	inline friend void
	sc_trace(sc_trace_file *tf, const work & v,
	    const std::string & NAME )
	{
	 	sc_trace(tf,v.dims[0], NAME + ".dims[0]");
		sc_trace(tf,v.dims[1], NAME + ".dims[1]");
		sc_trace(tf,v.wg_width, NAME + ".wg_width");
	}

	/** Comparator.
	 * @param v Object to compare to this.
	 * @return true iff objects are equal.
	 */
	inline bool
	operator==(const work & v) const
	{
		unsigned int i;

		if (bufs != v.bufs)
			return false;

		for (i = 0; i < bufs; i++)
			if (buf_map[i] != v.buf_map[i])
				return false;

		return (dims[0] == v.dims[0] && dims[1] == v.dims[1] &&
			wg_width == v.wg_width && ws_sched == v.ws_sched);
	}

	/** Copy constructor.
	 * @param v Object to copy data from into this object.
	 * @return New object with same data as v.
	 */
	inline work &
	operator=(const work &v)
	{
		unsigned int i;

		dims[0] = v.dims[0];
		dims[1] = v.dims[1];
		wg_width = v.wg_width;
		imem = v.imem;
		bufs = v.bufs;
		sp_bufs = v.sp_bufs;
		ws_sched = v.ws_sched;

		for (i = 0; i < bufs; i++)
			buf_map[i] = v.buf_map[i];

		for (i = 0; i < sp_bufs; i++)
			sp_buf_map[i] = v.sp_buf_map[i];

		return *this;
	}
};


/** Parameters for a single workgroup. */
template <unsigned int THREADS = 1024, unsigned int LANES = 128>
class workgroup {
public:
	/** X component of 2D thread ID offset in this workgroup (thread 0). */
	sc_uint<27> off_x;

	/** Y component of 2D thread ID offset in this workgroup (thread 0). */
	sc_uint<32> off_y;

	/** Workgroup dimensions.
	 * Y = THREADS / width. */
	sc_uint<const_log2(THREADS/LANES)> last_warp;

	/** @todo Convey the number of threads active. Do they start counting
	 * from lane 0, or do we need to send a mask instead? */

	/** SystemC mandatory print stream operation */
	inline friend std::ostream&
	operator<<(std::ostream& os, workgroup<THREADS,LANES> const &v)
	{
		os << "workgroup(THREADS: " << (LANES * (v.last_warp + 1)) <<
				"; "<< (32 * v.off_x) << "," << v.off_y << ")";
		return os;
	}


	/** SystemC mandatory trace output.
	 * @param tf Reference to trace file.
	 * @param v Control stack entry to print.
	 * @param NAME Name of the SystemC object.
	 */
	inline friend void
	sc_trace(sc_trace_file *tf, const workgroup<THREADS,LANES> & v,
	    const std::string & NAME )
	{
	 	sc_trace(tf,v.off_x, NAME + ".off_x");
		sc_trace(tf,v.off_y, NAME + ".off_y");
		sc_trace(tf,v.last_warp, NAME + ".last_warp");
	}

	/** Comparator.
	 * @param v Object to compare to this.
	 * @return true iff objects are equal.
	 */
	inline bool
	operator==(const workgroup<THREADS,LANES> & v) const
	{
		return (off_x == v.off_x && off_y == v.off_y &&
				last_warp == v.last_warp);
	}

	/** Copy constructor.
	 * @param v Object to copy data from into this object.
	 * @return New object with same data as v.
	 */
	inline workgroup<THREADS,LANES>& operator=
			(const workgroup<THREADS,LANES> & v)
	{
		off_x = v.off_x;
		off_y = v.off_y;
		last_warp = v.last_warp;
		return *this;
	}
};

}

#endif /* COMPUTE_MODEL_WORK_H */
