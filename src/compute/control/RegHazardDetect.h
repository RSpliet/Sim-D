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

#ifndef COMPUTE_CONTROL_REGHAZARDDETECT_H
#define COMPUTE_CONTROL_REGHAZARDDETECT_H

#include <systemc>

#include "model/reg_read_req.h"
#include "model/request_target.h"
#include "util/defaults.h"

using namespace compute_model;
using namespace simd_model;
using namespace sc_dt;

namespace compute_control {

/** Hazard detection interface for register files.
 *
 * The register file has uniform behaviour: take requests, check for conflicts,
 * if no conflicts exist serve data. Whether conflicts exist depend on the
 * banking scheme, address translation and number of read/write ports per bank.
 * We use a RegHazardDetect object to simulate hazard detection for various
 * combinations (e.g. 3R1W single bank, 1R1W per-column bank, 1R1W multiple
 * banks with row->bank hashing) following the state design pattern. This allows
 * us to share the base RegFile functionality while avoiding SystemC module
 * inheritance challenges.
 * We allow overwriting any method by marking them virtual. Realistically
 * though, we expect the default implementation to be suitable for all
 * situations, provided the map_idx() is properly reimplemented. */
template <unsigned int THREADS, unsigned int LANES>
class RegHazardDetect {
protected:
	/** Number of read ports on the (V)RF. */
	unsigned int read_ports;

	/** Number of 32-bit words in a VRF SRAM bank word. */
	unsigned int vrf_bank_words;

	/**
	 * Maps a given register type+index to a SRAM bank and row.
	 * @param t Type of the register.
	 * @param idx Index to map.
	 * @param bank Reference to integer to store resulting bank.
	 * @param row Reference to integer to store resulting row.
	 */
	virtual void map_idx(RegisterType t,
			sc_uint<const_log2(THREADS) + 2> idx,
			unsigned int &bank, unsigned int &row) const = 0;

	/**
	 * Protected constructor.
	 * @param r Number of read ports on the SRAMs in the (V)RFs.
	 * @param bw Number of 32-bit words in a SRAM bank word.
	 */
	RegHazardDetect(unsigned int r, unsigned int bw)
	: read_ports(r), vrf_bank_words(bw) {}

public:
	/** Detect bank conflicts on the IDecode<->Regfile interface.
	 * @param req Read requests incoming to the Regfile.
	 * @return Bit vector, an element is true if a conflict would cause
	 *         this element to generate a bank conflict with an earlier
	 *         element.
	 */
	virtual sc_bv<3>
	execute_bank_conflict(reg_read_req<THREADS/LANES> req) const
	{
		sc_bv<3> conflict = 0;
		unsigned int bank[3];
		unsigned int row[3];

		/* Because there's no more than 3 read ports on the register
		 * interface, don't bother with code that's highly general. */
		if (read_ports >= 3)
			return conflict;

		map_idx(req.reg[0].type, req.reg[0].row * THREADS, bank[0],
				row[0]);
		map_idx(req.reg[1].type, req.reg[1].row * THREADS, bank[1],
				row[1]);
		map_idx(req.reg[2].type, req.reg[2].row * THREADS, bank[2],
				row[2]);

		switch (read_ports) {
		case 1:
			/** Prioritise port 2 > 1 > 0, helps forward progress
			 * in pipelined operand fetch. */
			if (req.r[0] && req.r[1] &&
			    req.reg[0].type == req.reg[1].type &&
			    bank[0] == bank[1] && row[0] != row[1])
				conflict[0] = Log_1;

			if (req.r[0] && req.r[2] &&
			    req.reg[0].type == req.reg[2].type &&
			    bank[0] == bank[2] && row[0] != row[2])
				conflict[0] = Log_1;

			if (req.r[1] && req.r[2] &&
			    req.reg[1].type == req.reg[2].type &&
			    bank[1] == bank[2] && row[1] != row[2])
				conflict[1] = Log_1;
			break;
		case 2:
			if (req.r[0] && req.r[1] && req.r[2] &&
			    req.reg[0].type == req.reg[1].type &&
			    req.reg[1].type == req.reg[2].type &&
			    bank[0] == bank[1] && bank[0] == bank[2] &&
			    row[0] != row[1] && row[0] != row[2] &&
			    row[1] != row[2])
				conflict[0] = Log_1;

			break;
		default:
			break;
		}

		return conflict;
	}

	/** Detect VRF bank conflicts on the DRAM interface.
	 *
	 * We assume only a single (read or write) port is used on this
	 * interface.
	 * @param idx Indexes of requested words from the VRF.
	 * @param mask Read/Write mask.
	 * @return Bit vector, an element is true if a conflict would cause
	 *         this element to generate a bank conflict with an earlier
	 *         element.
	 */
	virtual sc_bv<MC_BUS_WIDTH/4>
	access_vrf_bank_conflict(reg_offset_t<THREADS> idx[MC_BUS_WIDTH/4],
			sc_bv<MC_BUS_WIDTH/4> &mask) const
	{
		unsigned int i, j;
		sc_bv<MC_BUS_WIDTH/4> conflict = 0;

		unsigned int bank_i, bank_j;
		unsigned int row_i, row_j;

		for (i = 1; i < MC_BUS_WIDTH/4; i++) {
			if (!mask[i])
				continue;

			map_idx(REGISTER_VGPR, idx[i].row * THREADS + idx[i].lane,
					bank_i, row_i);

			for (j = 0; j < i; j++) {
				if (!mask[j])
					continue;

				map_idx(REGISTER_VGPR, idx[j].row * THREADS + idx[j].lane,
						bank_j, row_j);

				if (bank_i == bank_j && row_i != row_j)
					conflict[i] = Log_1;
			}
		}

		return conflict;
	};

#if SP_BUS_WIDTH != (MC_BUS_WIDTH/4)
	/* Templated methods don't work apparently... */
	virtual sc_bv<SP_BUS_WIDTH>
	access_vrf_bank_conflict(reg_offset_t<THREADS> idx[SP_BUS_WIDTH],
			sc_bv<SP_BUS_WIDTH> &mask) const
	{
		unsigned int i, j;
		sc_bv<SP_BUS_WIDTH> conflict = 0;

		unsigned int bank_i, bank_j;
		unsigned int row_i, row_j;

		for (i = 1; i < SP_BUS_WIDTH; i++) {
			if (!mask[i])
				continue;

			map_idx(REGISTER_VGPR, idx[i].row * THREADS + idx[i].lane,
					bank_i, row_i);

			for (j = 0; j < i; j++) {
				if (!mask[j])
					continue;

				map_idx(REGISTER_VGPR, idx[j].row * THREADS + idx[j].lane,
						bank_j, row_j);

				if (bank_i == bank_j && row_i != row_j)
					conflict[i] = Log_1;
			}
		}

		return conflict;
	};
#endif

	/** Detect an access/execute conflict.
	 *
	 * It's really just not allowed for a wg to both process a DRAM request
	 * and perform requests from the pipeline at the same time. Access and
	 * execute run in different clock domains, it gets hairy, don't do it.
	 * When a DRAM request is outstanding, a work-group must be blocked.
	 * @param access_reg Register accessed by DRAM controller (access).
	 * @param exec_reg Register accessed by compute pipeline (execute).
	 */
	virtual bool
	ae_hazard(AbstractRegister access_reg,
			Register<THREADS/LANES> exec_reg) const
	{
		if (exec_reg.type == REGISTER_IMM)
			return false;

		return access_reg.wg == exec_reg.wg;
	}

	/** Set the number of 32-bit words in a vector register file bank word.
	 * @param w Number of 32-bit words in a VRF bank word. */
	virtual void
	set_vrf_bank_words(unsigned int w)
	{
		vrf_bank_words = w;
	}
};

}

#endif /* COMPUTE_CONTROL_REGHAZARDDETECT_H */
