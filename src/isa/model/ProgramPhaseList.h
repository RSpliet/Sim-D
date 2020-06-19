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

#ifndef ISA_MODEL_PROGRAMPHASELIST_H
#define ISA_MODEL_PROGRAMPHASELIST_H

#include "isa/model/DAG.h"
#include "util/ddr4_lid.h"

namespace isa_model {

/** Program phase types. */
typedef enum {
	PHASE_ACCESS_DRAM = 0,
	PHASE_ACCESS_SP,
	PHASE_EXECUTE,
	PHASE_SENTINEL
} program_phase_t;

/** List of program phases.
 *
 * This class both stores the list of program phases (data type) as well as all
 * methods that compute WCETs and WCET bounds (control) from this list.
 */
class ProgramPhaseList {
private:
	/** List of program phases. Pairs of \<type, cost\>. */
	std::vector<std::pair<program_phase_t, unsigned long> > phases;
public:
	/** Construct a ProgramPhaseList from a DAG */
	ProgramPhaseList(DAG *dag, unsigned long pipe_depth);

	/** Return the WCET for this ProgramPhaseList
	 * @param workgroups Number of work-groups that execute this phase list.
	 * @return The WCET in compute cycles. */
	unsigned long WCET(unsigned long workgroups);

	/** Return a lower bound on the WCET based on all resources running
	 * in parallel at maximum rate, without dependencies between phases.
	 * @param dram DRAM timing parameters
	 * @param workgroups Number of work-groups executing this phase list.
	 * @return A non-tight lower bound WCET, inflated for DRAM refresh. */
	unsigned long PerfectParallelismWCETLB(const dram::dram_timing *dram,
			unsigned long workgroups);

	/** Return a lower bound on the WCET based on always having two work-
	 * groups running in parallel, independent of whether they might use
	 * overlapping resources.
	 * @param workgroups Number of work-groups executing this phase list.
	 * @return A non-tight lower bound WCET, not inflated for DRAM refresh.*/
	unsigned long DoubleBufferedWCETLB(unsigned long workgroups);

	/** Return an upper bound on the WCET, assuming serial execution of all
	 * phases.
	 * @param workgroups Number of work-groups executing this phase list
	 * @return An upper bound WCET, not inflated for DRAM refresh. */
	unsigned long SingleBufferedWCET(unsigned long workgroups);

	/** Get the number of phases in this phase list.
	 * @return The number of phases in this phase list. */
	unsigned int countPhases(void) const;

	/** Print the BB properties.
	 * @param os Output stream.
	 * @param v The ProgramPhaseList to print.
	 * @return Output Stream. */
	inline friend std::ostream &
	operator<<(std::ostream &os, ProgramPhaseList &v)
	{
		unsigned int i;
		os << "ProgramPhaseList: " << v.phases.size() << " phases" << endl;

		for (i = 0; i < v.phases.size(); i++) {
			std::pair<program_phase_t, unsigned long> &p = v.phases[i];
			switch (p.first) {
			case PHASE_ACCESS_DRAM:
				os << "  ACCESS_DRAM(";
				break;
			case PHASE_ACCESS_SP:
				os << "  ACCESS_SP(";
				break;
			case PHASE_EXECUTE:
				os << "  EXECUTE(";
				break;
			default:
				break;
			}

			os << p.second << ")" << endl;
		}

		return os;
	}
};

}

#endif /* SRC_SRC_ISA_MODEL_PROGRAMPHASELIST_H */
