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

#include <systemc>

#include "util/sched_opts.h"

using namespace std;
using namespace sc_core;
using namespace sc_dt;

const pair<string,string> wss_opts[WSS_SENTINEL] = {
	[WSS_PAIRWISE_WG] = {"pairwise_wg","Keep work-groups in pairs, let both finish before launching two new work-groups."},
	[WSS_NO_PARALLEL_COMPUTE_SP] = {"no_parallel_compute_sp","Do not allow an SP<->compute transfer to run in parallel with compute."},
	[WSS_NO_PARALLEL_DRAM_SP] = {"no_parallel_dram_sp","Do not allow an SP<->compute transfer to run in parallel with DRAM access."},
	[WSS_STOP_SIM_FINI] = {"stop_sim_fini","Stop simulation once the kernel ends. This option is always on, exists only to aid unit-tests."},
	[WSS_STOP_DRAM_FINI] = {"stop_sim_fini","Stop simulation once the DRAM controller processed its last stride descriptor. This option is always off, exists only for WCET analysis."},
};

bool
wss_opts_validate(sc_bv<WSS_SENTINEL> sched_opts)
{
	if (sched_opts[WSS_NO_PARALLEL_COMPUTE_SP] &&
	    sched_opts[WSS_NO_PARALLEL_DRAM_SP]) {
		cerr << "Error: Scheduling options \"" <<
				wss_opts[WSS_NO_PARALLEL_COMPUTE_SP].first <<
				"\" and \"" <<
				wss_opts[WSS_NO_PARALLEL_DRAM_SP].first <<
				"\" cannot both be active at the same time." << endl;
		return false;
	}

	return true;
}
