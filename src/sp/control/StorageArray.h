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

#ifndef SP_CONTROL_STORAGEARRAY_H
#define SP_CONTROL_STORAGEARRAY_H

#include <systemc>
#include <cstdint>

#include "model/Register.h"
#include "util/constmath.h"
#include "sp/model/DQ_reservation.h"

using namespace sc_dt;
using namespace sc_core;
using namespace std;
using namespace sp_model;

namespace sp_control {

/** Simulation model for a scratchpad.
 * This assumes a hardware solution for unaligned accesses, like described in
 * US patent 6256253 "Memory device with support for unaligned access".
 */
template<unsigned int WG, unsigned int BUS_WIDTH_DRAM, unsigned int BUS_WIDTH, unsigned int SIZE_BYTES>
class StorageArray : public sc_module
{
public:
	/** SDR clock */
	sc_in<bool> in_clk{"in_clk"};

	/** Data output, to both DRAM and RF. */
	sc_inout<sc_uint<32> > out_data[BUS_WIDTH];

	/**************** DQ interface **************************/
	/** Input signals from the DQ schedulers */
	sc_in<dq_pipe_sa<BUS_WIDTH> > in_dq_cmd{"in_dq_cmd"};

	/** Data coming from the RF. */
	sc_in<sc_uint<32> > in_rf_data[BUS_WIDTH];

	/** Mask returned by the RF upon write. Used to skip disabled threads */
	sc_in<sc_bv<BUS_WIDTH> > in_rf_mask;

	/**************** DRAM interface *************************/
	/** Enable signal, when 1 perform read or write */
	sc_in<bool> in_dram_enable{"in_dram_enable"};

	/** Destination of current DRAM traffic, to filter out writes to this
	 * specific storage array. */
	sc_in<RequestTarget> in_dram_dst{"in_dram_dst"};

	/** True iff operation is a write op */
	sc_in<bool> in_dram_write{"in_dram_write"};

	/** Address to manipulate */
	sc_in<sc_uint<18> > in_dram_addr{"in_dram_addr"};

	/** Data bus for write data */
	sc_in<sc_uint<32> > in_dram_data[BUS_WIDTH_DRAM];

	/** Write mask, aligned to in_data. */
	sc_in<sc_bv<BUS_WIDTH_DRAM> > in_dram_mask{"in_dram_mask"};

	/** Constructor */
	SC_CTOR(StorageArray)
	{
		SC_THREAD(thread);
		sensitive << in_clk.pos();
	}

	/** Directly read a value to the scratchpad, for validation purposes.
	 * @param addr Address to read.
	 * @return Value written at given offset. */
	uint32_t
	debug_sp_read(sc_uint<18> addr)
	{
		return sp[(addr & 0xc) >> 2][(addr & 0x3fff0) >> 4];
	}

	/** Directly write a value to scratchpad, for testing/debugging
	 * purposes.
	 * @param addr Scratchpad address to write to.
	 * @param val Value to write back. */
	void
	debug_sp_write(sc_uint<18> addr, uint32_t val)
	{
		sp[(addr & 0xc) >> 2][(addr & 0x3fff0) >> 4] = val;
	}


	/** Print a range of the scratchpad to stdout.
	 * @param addr Start address.
	 * @param words Number of words to print.*/
	void
	debug_print_range(sc_uint<18> addr, unsigned int words)
	{

		unsigned int i;

		for (i = addr; i < addr + (words * 4); i+= 4) {
			if (!(i & 0xf))
				std::cout << "SP@" << std::hex << i << ": ";


			std::cout << debug_sp_read(i) << " ";

			if ((i & 0xf) >= 0xc)
				 std::cout << std::dec << std::endl;
		}
	}

	/** Upload a test pattern to the scratchpad.
	 * For now it just counts upwards from 0.
	 * @param addr Start address.
	 * @param words Number of words to upload. */
	void
	debug_upload_test_pattern(sc_uint<18> addr, unsigned int words)
	{
		uint32_t i;

		for (i = addr; i < addr + (words * 4); i+= 4)
			debug_sp_write(i, i - addr);
	}

private:
	/** Scratchpad data storage */
	uint32_t sp[BUS_WIDTH][SIZE_BYTES/(4*BUS_WIDTH)];

	/** Perform a single read */
	void
	dram_read(void)
	{
		sc_uint<18> addr;
		unsigned int align_phase;
		unsigned int idx;
		unsigned int i;
		unsigned int bank;
		unsigned int lbank;
		unsigned int lidx;
		uint32_t sp_entry;

		addr = in_dram_addr.read();
		/** @todo This test was bogus. Should be replaced with something
		 * meaningful iff in_mask is truly transmitted for reads */
		//assert(addr + ((in_words.read() +  1) * 4) < SIZE_BYTES);

		align_phase = (addr >> 2) & ((BUS_WIDTH)-1);
		bank = align_phase & ~(BUS_WIDTH_DRAM-1);
		idx = addr >> const_log2(BUS_WIDTH*4);

		for (i = 0; i < BUS_WIDTH_DRAM; i++) {
			lbank = bank + i;
			lidx = idx;
			if (lbank < align_phase)
				lbank += (BUS_WIDTH_DRAM);

			if (lbank >= BUS_WIDTH) {
				lbank = i;
				lidx = idx + 1;
			}

			/** @todo could lead to out-of-bounds read */
			sp_entry = sp[lbank][lidx];
			out_data[i].write(sp_entry);
		}
	}

	/** Perform a single write */
	void
	dram_write(void)
	{
		sc_uint<18> addr;
		unsigned int align_phase;
		unsigned int bank;
		unsigned int idx;
		unsigned int lbank;
		unsigned int lidx;
		unsigned int i;
		sc_bv<BUS_WIDTH_DRAM> mask;

		addr = in_dram_addr.read();
		mask = in_dram_mask.read();
		assert(addr + (__builtin_popcount(mask.to_uint()) * 4) < SIZE_BYTES);

		align_phase = (addr >> 2) & ((BUS_WIDTH)-1);
		bank = align_phase & ~(BUS_WIDTH_DRAM-1);
		idx = addr >> const_log2(BUS_WIDTH*4);

		for (i = 0; i < BUS_WIDTH_DRAM; i++) {
			if (!mask[i])
				continue;

			lbank = bank + i;
			lidx = idx;
			if (lbank < align_phase)
				lbank += (BUS_WIDTH_DRAM);

			if (lbank >= BUS_WIDTH) {
				lbank = i;
				lidx = idx + 1;
			}

			sp[lbank][lidx] = in_dram_data[i].read();
		}
	}

	/** Perform a single read.
	 * @param psa StorageArray command. */
	void
	rf_read(dq_pipe_sa<BUS_WIDTH> &psa)
	{
		sc_uint<18> addr;
		unsigned int align_phase;
		unsigned int idx;
		unsigned int i;
		uint32_t sp_entry;

		addr = psa.addr;
		/** @todo This test was bogus. Should be replaced with something
		 * meaningful iff in_mask is truly transmitted for reads */
		//assert(addr + ((in_words.read() +  1) * 4) < SIZE_BYTES);

		align_phase = (addr >> 2) & ((BUS_WIDTH)-1);
		idx = addr >> const_log2(BUS_WIDTH*4);

		for (i = 0; i < BUS_WIDTH; i++) {
			/** @todo could lead to out-of-bounds read */
			sp_entry = sp[i][(i < align_phase ? idx + 1 : idx)];
			out_data[i].write(sp_entry);
		}
	}

	/** Perform a single write.
	 * @param psa Storagearray command. */
	void
	rf_write(dq_pipe_sa<BUS_WIDTH> &psa)
	{
		sc_uint<18> addr;
		unsigned int align_phase;
		unsigned int idx;
		unsigned int i;
		sc_bv<BUS_WIDTH> mask;

		addr = psa.addr;
		mask = psa.mask_w & in_rf_mask.read();
		assert(addr + (__builtin_popcount(mask.to_uint()) * 4) < SIZE_BYTES);

		align_phase = (addr >> 2) & ((BUS_WIDTH)-1);
		idx = addr >> const_log2(BUS_WIDTH*4);

		for (i = 0; i < BUS_WIDTH; i++) {
			if (mask[i])
				sp[i][(i < align_phase ? idx + 1 : idx)] =
						in_rf_data[i].read();
		}
	}

	/** Thread for DRAM/RF communication. */
	void
	thread(void)
	{
		dq_pipe_sa<BUS_WIDTH> psa;
		RequestTarget dst;

		while (true) {
			psa = in_dq_cmd.read();
			dst = in_dram_dst.read();

			if (in_dram_enable.read() && dst == RequestTarget(WG,TARGET_SP)) {
				assert(!psa.rw);

				if (in_dram_write.read())
					dram_write();
				else
					dram_read();
			} else if (psa.rw) {
				assert(!(in_dram_enable.read() &&
					dst == RequestTarget(WG,TARGET_SP)));

				if (psa.write_w)
					rf_write(psa);
				else
					rf_read(psa);
			}
			wait();
		}
	}
};

}

#endif /* SP_CONTROL_SCRATCHPAD_H */
