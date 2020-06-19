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

#include <unistd.h>

#include "mc/model/cmdarb_stats.h"

using namespace mc_model;

void
cmdarb_stats::min(cmdarb_stats &s) {
	act_c = std::min(act_c, s.act_c);
	pre_c = std::min(pre_c, s.pre_c);
	cas_c = std::min(cas_c, s.cas_c);
	ref_c = std::min(ref_c, s.ref_c);
	lda = std::min(lda, s.lda);
	lid = std::min(lid, s.lid);
	power = std::min(power, s.power);
	energy = std::min(energy, s.energy);
	bytes = std::min(bytes, s.bytes);
}

void
cmdarb_stats::max(cmdarb_stats &s) {
	act_c = std::max(act_c, s.act_c);
	pre_c = std::max(pre_c, s.pre_c);
	cas_c = std::max(cas_c, s.cas_c);
	ref_c = std::max(ref_c, s.ref_c);
	lda = std::max(lda, s.lda);
	lid = std::max(lid, s.lid);
	power = std::max(power, s.power);
	energy = std::max(energy, s.energy);
	bytes = std::max(bytes, s.bytes);
}

void
cmdarb_stats::aggregate(cmdarb_stats &s) {
	act_c += s.act_c;
	pre_c += s.pre_c;
	cas_c += s.cas_c;
	ref_c += s.ref_c;
	lda += s.lda;
	lid += s.lid;
	power += s.power;
	energy += s.energy;
	bytes += s.bytes;
}
