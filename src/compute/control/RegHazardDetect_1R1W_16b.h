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

#ifndef COMPUTE_CONTROL_REGHAZARDDETECT_1R1W_16B_H
#define COMPUTE_CONTROL_REGHAZARDDETECT_1R1W_16B_H

#include "compute/control/RegHazardDetect.h"

using namespace compute_model;
using namespace simd_model;
using namespace sc_dt;

namespace compute_control {

/** Register hazard detection for an 32-bank (16 per-warp) 1R1W register
 * file.
 * @param THREADS number of threads in a warp.
 * @param LANES Number of SIMD lanes per SIMD-cluster.
 */
template <unsigned int THREADS, unsigned int LANES>
class RegHazardDetect_1R1W_16b : public RegHazardDetect<THREADS,LANES>
{
private:
	void
	map_idx(RegisterType t, sc_uint<const_log2(THREADS) + 2> idx,
			unsigned int &bank, unsigned int &row) const
	{
		switch (t) {
		case REGISTER_VGPR:
			/* Low bank bits are based on overlapping of both
			 * THREADS/2 halves, binary or with the modulo 4 of the
			 * vector index. */
			bank = idx & ((THREADS/2) - 1);
			bank |= (idx & (3 << const_log2(THREADS))) >> 1;
			bank >>= const_log2(this->vrf_bank_words);

			/* First row bit from which half of THREADS/2 width is
			 * accessed. Or with remainder of index. */
			row = (idx >> const_log2(THREADS/2)) & 1;
			row |= (idx >> const_log2(2 * THREADS)) & ~1;
			break;
		case REGISTER_SGPR:
			bank = idx;
			row = 0;
			break;
		default:
			/* Row will always match, never conflict */
			bank = 0;
			row = 0;
			break;
		}
	}

public:
	RegHazardDetect_1R1W_16b() : RegHazardDetect<THREADS,LANES>(1,4) {}
};

}

#endif /* COMPUTE_CONTROL_REGHAZARDDETECT_1R1W_16B_H */
