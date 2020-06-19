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

#ifndef UTIL_SCHED_OPTS_H
#define UTIL_SCHED_OPTS_H

#include <systemc>
#include <utility>
#include <string>

typedef enum {
	WSS_PAIRWISE_WG = 0,
	WSS_NO_PARALLEL_COMPUTE_SP = 1,
	WSS_NO_PARALLEL_DRAM_SP = 2,
	WSS_STOP_SIM_FINI = 3,
	WSS_STOP_DRAM_FINI = 4,
	WSS_SENTINEL,
} workgroup_sched_policy;

extern const std::pair<std::string,std::string> wss_opts[WSS_SENTINEL];

bool wss_opts_validate(sc_dt::sc_bv<WSS_SENTINEL> sched_opts);

#endif /* UTIL_SCHED_OPTS_H */
