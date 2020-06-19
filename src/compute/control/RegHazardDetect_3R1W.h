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

#ifndef COMPUTE_CONTROL_REGHAZARDDETECT_3R1W_H
#define COMPUTE_CONTROL_REGHAZARDDETECT_3R1W_H

#include "compute/control/RegHazardDetect.h"

using namespace compute_model;
using namespace simd_model;
using namespace sc_dt;

namespace compute_control {

/** Register hazard detection for a two-bank (one per-warp) 3R1W register file.
 * @param THREADS number of threads in a warp.
 * @param LANES Number of SIMD lanes per SIMD-cluster.
 */
template <unsigned int THREADS, unsigned int LANES>
class RegHazardDetect_3R1W : public RegHazardDetect<THREADS,LANES>
{
private:
	void
	map_idx(RegisterType t, sc_uint<const_log2(THREADS) + 2> idx,
			unsigned int &bank, unsigned int &row) const
	{
		switch (t) {
		case REGISTER_VGPR:
			bank = (idx & (LANES - 1)) / this->vrf_bank_words;
			row = idx >> const_log2(LANES);
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
	RegHazardDetect_3R1W() : RegHazardDetect<THREADS,LANES>(3,4) {}
};

}

#endif /* COMPUTE_CONTROL_REGHAZARDDETECT_3R1W_H */
