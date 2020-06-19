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

#include "isa/model/ProgramPhaseList.h"
#include "util/debug_output.h"

using namespace isa_model;
using namespace dram;
using namespace std;

ProgramPhaseList::ProgramPhaseList(DAG *dag, unsigned long pipe_depth)
{
	DAGNode *n;
	pair<unsigned long, DAGNode *> edge;
	unsigned long execute;

	n = dag->getSource();
	execute = pipe_depth;

	if (debug_output[DEBUG_WCET_PROGRESS])
		cout << "* Critical path to program phase list transformation."
				<< endl;

	while (n != dag->getSink()) {
		if (n->outCount() > 1)
			throw invalid_argument("DAG is not a critical path.");

		edge = *(n->out_cbegin());
		execute += edge.first;
		if (n->getXSType() != XS_NONE) {
			phases.emplace_back(PHASE_EXECUTE, execute);
			phases.emplace_back((n->getXSType() == XS_SP ? PHASE_ACCESS_SP : PHASE_ACCESS_DRAM),
					n->getXSCost());
			execute = 0ul;
		}

		n = edge.second;
	}
}

unsigned long
ProgramPhaseList::WCET(unsigned long workgroups)
{
	unsigned long wcet_1t;
	unsigned long wcet_interleaved;
	unsigned long w;
	unsigned int i;

	wcet_1t = 0ul;
	wcet_interleaved = 0ul;

	for (i = 0; i < phases.size(); i++) {
		wcet_1t += phases[i].second;
		wcet_interleaved += max(phases[i].second,
				phases[(i+1) % phases.size()].second);
	}

	w = wcet_interleaved * (workgroups >> 1);
	if (workgroups % 2) {
		/* Odd # workgroups.*/
		w += wcet_1t;
	} else {
		/* Even # WGs */
		w += min(phases[0].second,phases[phases.size() - 1].second);
	}

	return w;
}

unsigned long
ProgramPhaseList::PerfectParallelismWCETLB(const dram_timing *dram,
		unsigned long workgroups)
{
	unsigned long wcet[PHASE_SENTINEL];
	unsigned long w;
	unsigned int i;

	for (i = 0; i < PHASE_SENTINEL; i++)
		wcet[i] = 0ul;

	for (i = 0; i < phases.size(); i++)
		wcet[phases[i].first] += phases[i].second;

	w = 0ul;

	for (i = 0; i < PHASE_SENTINEL; i++) {
		wcet[i] *= workgroups;
		if (i == unsigned(PHASE_ACCESS_DRAM))
			wcet[i] = inflate_refresh(dram, wcet[i]);
		w = max(w, wcet[i]);
	}

	return w;
}

unsigned long
ProgramPhaseList::DoubleBufferedWCETLB(unsigned long workgroups)
{
	unsigned long wcet_1t;
	unsigned int i;

	wcet_1t = 0ul;

	for (i = 0; i < phases.size(); i++)
		wcet_1t += phases[i].second;

	return wcet_1t * div_round_up(workgroups, 2);
}

unsigned long
ProgramPhaseList::SingleBufferedWCET(unsigned long workgroups)
{
	unsigned long wcet_1t;
	unsigned int i;

	wcet_1t = 0ul;

	for (i = 0; i < phases.size(); i++)
		wcet_1t += phases[i].second;

	return wcet_1t * workgroups;
}

unsigned int
ProgramPhaseList::countPhases(void) const
{
	return phases.size();
}
