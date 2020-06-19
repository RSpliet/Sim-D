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

#ifndef MC_CONTROL_CMDGEN_DDR4_H
#define MC_CONTROL_CMDGEN_DDR4_H

#include <cstdint>
#include <array>
#include <systemc>
#include <tlm>

#include "util/constmath.h"
#include "mc/model/burst_request.h"
#include "mc/model/cmd_DDR.h"

using namespace std;
using namespace sc_core;
using namespace sc_dt;
using namespace mc_model;
using namespace tlm;

namespace mc_control {

/**
 * Perform address->(bank, col, row) translation and generate DRAM commands
 * from address/word mask pairs.
 * DDR4 uses pairs of banks from different bank groups to optimise for the
 * common case of unit-stride transfers.
 */
template <unsigned int BUS_WIDTH, unsigned int DRAM_BANKS,
	unsigned int DRAM_COLS, unsigned int DRAM_ROWS, unsigned int THREADS>
class CmdGen_DDR4 : public sc_module
{
private:
	/** Constant used as bank inactive value.
	 * @return Inactive bank constant. */
	constexpr sc_uint<const_log2(DRAM_ROWS) + 1>
	bank_inactive(void) const
	{
		return sc_uint<const_log2(DRAM_ROWS) + 1>(DRAM_ROWS + (DRAM_ROWS-1));
	}

	/** Currently active row for each bank. */
	sc_uint<const_log2(DRAM_ROWS) + 1> bank_active_row[DRAM_BANKS];
public:
	/** DRAM clock, SDR */
	sc_in<bool> in_clk{"in_clk"};

	/** Incoming burst requests */
	sc_fifo_in<burst_request<BUS_WIDTH,THREADS> > in_req_fifo{"in_req_fifo"};

	/** One FIFO per bank - CAS/Precharge commands
	 * @todo FIFO depth? */
	sc_port<tlm_fifo_put_if<cmd_DDR<BUS_WIDTH,THREADS> > >
						out_fifo[DRAM_BANKS];

	/** True iff processing the current stride or set of indexes */
	sc_inout<bool> out_busy{"out_busy"};

	/** Construct thread, initialise LUT values */
	SC_CTOR(CmdGen_DDR4)
	{
		unsigned int i;

		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();

		for (i = 0; i < DRAM_BANKS; i++)
			bank_active_row[i] = bank_inactive();
	}

	/** Translate address to row/bank/col offsets. Simple bit-gathering
	 * that should translate to no logic.
	 * Example translation for BUS_WIDTH = 16 (64-bits), DRAM_BANKS=16,
	 * 			   COLS = 1024, ROWS = 32768:
	 * Bank: addr[16:14]:addr[6]
	 * Col : addr[13:7](:addr[5:3]) - low 3 col bits for burst order masked
	 * 				  off
	 * Row : addr[31:17]
	 */
	void
	address_translate(sc_uint<32> addr,
			sc_uint<const_log2(DRAM_BANKS)> &bank,
			sc_uint<const_log2(DRAM_ROWS)> &row,
			sc_uint<const_log2(DRAM_COLS)> &col)
	{
		uint64_t offset;

		offset = const_log2(BUS_WIDTH) + const_log2(DRAM_COLS) - 1;

		bank = ((addr >> (const_log2(BUS_WIDTH) + 2)) & 0x1) |
			((addr >> offset) & (DRAM_BANKS - 2));

		col = (addr >> const_log2(BUS_WIDTH)) & ((DRAM_COLS) - 8);

		offset += const_log2(DRAM_BANKS);
		row = (addr >> offset) & (DRAM_ROWS - 1);
	}

	/** Work out whether the precharge policy mandates a precharge when
	 * translating this burst request into a DDR command.
	 *
	 * This command may issue a second precharge command to the other bank
	 * in the bank-pair.
	 * @param req Burst request.
	 * @param rwp Outgoing command, may be modified. */
	void
	precharge(burst_request<BUS_WIDTH,THREADS> req,
			cmd_DDR<BUS_WIDTH,THREADS> *rwp)
	{
		unsigned int i;
		static cmd_DDR<BUS_WIDTH,THREADS> rwp_pre{.row = 0,
				.col = 0,
				.pre_pre = 0,
				.act = 0,
				.read = 0,
				.write = 0,
				.pre_post = 1,
				.wordmask = 0};

		sc_uint<const_log2(DRAM_BANKS)> bank, next_bank;
		sc_uint<const_log2(DRAM_ROWS)> row, next_row;
		sc_uint<const_log2(DRAM_COLS)> col, next_col;

		address_translate(req.addr, bank, row, col);

		/* Rely on the compiler to eliminate irrelevant code...? */
		switch (req.pre_pol) {
		case PRECHARGE_ALAP:
			if (row != bank_active_row[bank]) {
				if (bank_active_row[bank] != bank_inactive()) {
					rwp->pre_pre = true;
					rwp->pre_post = false;
				}

				bank_active_row[bank] = row;
			}

			if (req.addr_next == 0xffffffff) {
				rwp->pre_post = true;
				bank_active_row[bank] = bank_inactive();

				/* Must close other banks too */
				for (i = 1; i < DRAM_BANKS; i++) {
					next_bank = (bank + i) % DRAM_BANKS;
					if (bank_active_row[next_bank] != bank_inactive()) {
						rwp_pre.target = req.target;
						out_fifo[next_bank]->put(rwp_pre);
						bank_active_row[next_bank] = bank_inactive();
					}

				}
			}

			break;
		case PRECHARGE_LINEAR:
		default:
			bank_active_row[bank] = row;

			address_translate(req.addr_next, next_bank, next_row,
					next_col);
			if ((next_bank & (DRAM_BANKS - 2)) !=
				(bank & (DRAM_BANKS - 2)) ||
			    next_row != row) {
				rwp->pre_post = true;
				/**
				 * @todo Assignment to bank_active_row is
				 * sequential, conflicts with the activate logic
				 * in thread_lt if this were an HDL.
				 */
				bank_active_row[bank] = bank_inactive();

				if (bank_active_row[bank ^ 0x1] !=
						bank_inactive()) {

					rwp_pre.target = req.target;
					out_fifo[bank ^ 0x1]->put(rwp_pre);
					bank_active_row[bank ^ 0x1] =
							bank_inactive();
				}
			}
			break;
		}
	}

private:
	/** Reset logic. */
	void
	reset(void)
	{
		out_busy.write(false);
	}

	/** Main thread */
	void
	thread_lt(void)
	{
		unsigned int i;

		burst_request<BUS_WIDTH,THREADS> req;
		sc_uint<const_log2(DRAM_BANKS)> bank = 0;
		sc_uint<const_log2(DRAM_ROWS)> row = 0;
		sc_uint<const_log2(DRAM_COLS)> col = 0;

		cmd_DDR<BUS_WIDTH,THREADS> rwp;

		reset();

		while (true) {
			/* blocking */
			in_req_fifo.read(req);

			out_busy.write(!req.last);

			address_translate(req.addr, bank, row, col);

			rwp.act = 0;

			if (bank_active_row[bank] != row)
				rwp.act = 1;

			rwp.col = col;
			rwp.row = row;
			rwp.pre_pre = false;
			rwp.write = req.write;
			rwp.read = !req.write;
			rwp.pre_post = false;
			rwp.wordmask = req.wordmask;
			rwp.sp_offset = req.sp_offset;
			rwp.target = req.target;

			for (i = 0; i < BUS_WIDTH; i++)
				rwp.reg_offset[i] = req.reg_offset[i];

			precharge(req, &rwp);

			out_fifo[bank]->put(rwp);

			wait();
		}
	}
};

}

#endif /* MC_CONTROL_CMDGEN_DDR4_H_ */
