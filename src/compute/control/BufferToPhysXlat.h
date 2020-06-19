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

#ifndef MC_BUFFERTOPHYSXLAT_H
#define MC_BUFFERTOPHYSXLAT_H

#include <cstdint>
#include <array>
#include <systemc>

#include "util/constmath.h"
#include "model/Buffer.h"

using namespace simd_model;
using namespace sc_core;
using namespace sc_dt;
using namespace std;

namespace compute_control {

/** Translation lookup table from buffer ID to physical address.
 *
 * This architecture works with mapped buffers instead of page tables.
 * Accelerator workloads generally only use a limited number of buffers anyway,
 * and as a work-around one could allocate a big buffer for all data if
 * the number of buffers proves insufficient. This approach has several
 * benefits:
 *
 * - Enforce buffer alignment on the hardware level, makes RT analysis easier
 *   for the common case.
 * - Don't worry about page table walking, which has unpredictable latency.
 */
template <unsigned int ENTRIES>
class BufferToPhysXlat : public sc_core::sc_module
{
public:
	/** Compute clock. */
	sc_in<bool> in_clk{"in_clk"};

	/** Synchronous reset signal. */
	sc_in<bool> in_rst{"in_rst"};

	/** Requested buffer index */
	sc_in<sc_uint<const_log2(ENTRIES)> > in_idx{"in_idx"};

	/** Buffer object for slot in_idx. */
	sc_inout<Buffer> out_phys{"out_phys"};

	/** Perform a write */
	sc_in<bool> in_w{"in_w"};

	/** Index to write to */
	sc_in<sc_uint<const_log2(ENTRIES)> > in_idx_w{"in_idx_w"};

	/** Physical address to set idx to */
	sc_in<Buffer> in_phys_w{"in_phys_w"};

	/** Construct thread */
	SC_CTOR(BufferToPhysXlat)
	{
		do_rst();

		SC_THREAD(thread_rd);
		sensitive << in_clk.pos();

		SC_THREAD(thread_wr);
		sensitive << in_clk.pos();
	}

	/** Test helper to set default values
	 * @param idx Buffer index.
	 * @param value Physical address for this buffer. */
	void
	set(unsigned int idx, Buffer value)
	{
		if (idx < ENTRIES)
			xlat_tbl[idx] = value;
	}

private:
	/** Translation table from buffer ID to physical address. */
	array<Buffer,ENTRIES> xlat_tbl;

	/** Perform a reset of the entries in this buffer translation table. */
	void
	do_rst(void)
	{
		unsigned int i;

		for (i = 0; i < ENTRIES; i++)
			xlat_tbl[i] = Buffer();
	}

	/** Main thread */
	void
	thread_rd(void)
	{
		while (true)
		{
			wait();

			if (in_rst.read()) {
				do_rst();
			} else {
				wait(SC_ZERO_TIME);
				out_phys->write(xlat_tbl[in_idx->read()]);
			}
		}
	}

	/** Write interface */
	void
	thread_wr(void)
	{
		Buffer b;

		while (true)
		{
			if (in_w.read())
				set(in_idx_w.read(), in_phys_w.read());

			wait();
		}
	}
};

}

#endif /* MC_BUFFERTOPHYSXLAT_H */
