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

#ifndef ISA_ANALYSIS_DRAMSIM_H
#define ISA_ANALYSIS_DRAMSIM_H

#include "isa/model/Program.h"
#include "model/workgroup_width.h"
#include "model/stride_descriptor.h"
#include "util/ddr4_lid.h"

namespace isa_analysis {

/** Compute program upload time.
 *
 * @param p Program to be uploaded,
 * @param dram DRAM timings for the current configuration,
 * @return Program upload latency in compute cycles. */
unsigned long
ProgramUploadTime(isa_model::Program &p, const dram::dram_timing *dram);

/** Calculate/simulate a worst-case DRAM request issue latency for each DRAM
 * request in the program. WCET stored as metadata inside the individual
 * instructions.
 *
 * @param p Program for which DRAM and scratchpad requests are bound,
 * @param w Width of a work-group, determines stride parameters,
 * @param dram DRAM timings for the current configuration,
 * @param sim Function pointer to method launching simulation. */
void DRAMSim(isa_model::Program &p, simd_model::workgroup_width w,
		const dram::dram_timing *dram,
		unsigned long (*sim)(simd_model::stride_descriptor &, bool));

}

#endif /* SRC_SRC_ISA_ANALYSIS_DRAMSIM_H */
