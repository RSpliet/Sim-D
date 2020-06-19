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

#ifndef SRC_INCLUDE_UTIL_DEBUG_OUTPUT_H_
#define SRC_INCLUDE_UTIL_DEBUG_OUTPUT_H_

#include <string>

using namespace std;

typedef enum {
	DEBUG_CMD_EMIT = 0,
	DEBUG_CMD_STATS,
	DEBUG_MEM_FE,
	DEBUG_COMPUTE_TRACE,
	DEBUG_COMPUTE_STALLS,
	DEBUG_COMPUTE_WG_STATUS,
	DEBUG_COMPUTE_WG_STATUS_CODE,
	DEBUG_COMPUTE_WG_DIST,
	DEBUG_PROGRAM,
	DEBUG_WCET_PROGRESS,
	DEBUG_SENTINEL
} debug_output_type;

extern const pair<string,string> debug_output_opts[DEBUG_SENTINEL];

extern bool debug_output[DEBUG_SENTINEL];

/** Initialise all debug output to 0. */
void debug_output_reset(void);

/** Validate the current set of debug output options.
 * @return true iff the current debug output option combination is valid.
 */
bool debug_output_validate(void);

#endif /* SRC_INCLUDE_UTIL_DEBUG_OUTPUT_H_ */
