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

#ifndef ISA_ANALYSIS_CYCLESIM_H
#define ISA_ANALYSIS_CYCLESIM_H

#include "isa/model/Program.h"
#include "compute/control/IDecode.h"

namespace isa_analysis {

/**
 * Perform a cycle-accurate simulation of a linear execution of a program (that
 * is: no branches taken, not even unconditional branches) for
 * given pipeline. The result is a per-BB cycle count, plus a per-edge cost.
 *
 * Must be run after control flow analysis, such that the edges to amend with
 * compute timing information exist. Lays the foundation for loop expansion and
 * critical path analysis.
 *
 * @param p Program to analyse.
 * @param idec_impl Specific IDecode implementation (1 or 3 cycles)
 * @param iexec_stages Number of pipeline stages in IExecute. Minimum 3.
 *
 * XXX: Implement for the three-stage IDecode.
 * XXX: Extract and store information in BBs.
 */
void CycleSim(isa_model::Program &p, compute_control::IDecode_impl idec_impl,
		unsigned int iexec_stages);

}

#endif /* ISA_ANALYSIS_CYCLESIM_H */
