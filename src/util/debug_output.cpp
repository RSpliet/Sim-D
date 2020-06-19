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

#include <iostream>

#include "util/debug_output.h"

using namespace std;

const pair<string,string> debug_output_opts[DEBUG_SENTINEL] = {
	[DEBUG_CMD_EMIT] = {"mc_cmd","Print every emitted DRAM command."},
	[DEBUG_CMD_STATS] = {"mc_stats","Print DRAM statistics at the end of execution."},
	[DEBUG_MEM_FE] = {"mem_fe","Print emitted DRAM requests and latency."},
	[DEBUG_COMPUTE_TRACE] = {"pipe_trace","Print exhaustive trace of every state in the pipeline."},
	[DEBUG_COMPUTE_STALLS] = {"pipe_stalls","Print information about each instruction that stalls in the decode pipeline phase."},
	[DEBUG_COMPUTE_WG_STATUS] = {"pipe_wg_status","Every cycle, print the workgroup status."},
	[DEBUG_COMPUTE_WG_STATUS_CODE] = {"pipe_wg_status_code","Every cycle, print the workgroup status coded to be plotted as a gnuplot heat map."},
	[DEBUG_COMPUTE_WG_DIST] = {"pipe_wg_dist","Print distribution events of workgroups to SimdCluster."},
	[DEBUG_PROGRAM] = {"prg","Print program."},
	[DEBUG_WCET_PROGRESS] = {"wcet_progress","Print verbose progress messages for WCET determination."},
};

bool debug_output[DEBUG_SENTINEL];

void
debug_output_reset(void)
{
	unsigned int i;

	for (i = 0; i < DEBUG_SENTINEL; i++)
		debug_output[i] = 0;
}

bool
debug_output_validate(void)
{
	unsigned int i;

	if (debug_output[DEBUG_COMPUTE_WG_STATUS_CODE]) {
		for (i = 0; i < DEBUG_SENTINEL; i++) {
			if (i == DEBUG_COMPUTE_WG_STATUS_CODE)
				continue;

			if (debug_output[i]) {
				cerr << "Error: Debug output option \"" <<
					debug_output_opts[DEBUG_COMPUTE_WG_STATUS_CODE].first <<
					"\" should not be combined with other output options." << endl;
				return false;
			}
		}
	}

	return true;
}
