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

#ifndef ISA_ANALYSIS_TIMINGDAG_H
#define ISA_ANALYSIS_TIMINGDAG_H

#include "isa/model/Program.h"
#include "isa/model/DAG.h"

namespace isa_analysis {

/** Return the critical path for a given DAG, provided access cost is equal
 * across all paths. */
isa_model::DAG *criticalPath(isa_model::DAG *in, bool sp_as_compute = false);

/** Construct a directed acyclic graph for given program. Expects program to
 * carry WCET information of both compute and DRAM, so after the CycleSim and
 * DRAMSim passes have run.
 */
isa_model::DAG *TimingDAG(isa_model::Program &prg);

}

#endif /* ISA_ANALYSIS_TIMINGDAG_H */
