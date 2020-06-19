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

#include <stdexcept>

#include <isa/model/Metadata.h>
#include <util/parse.h>

using namespace std;

namespace isa_model {

Metadata::Metadata()
: branch_taken(0), branch_not_taken(0), branch_cycle_pos(0),
  branch_cycle_pos_init(0), sd_words(0), sd_period(0), sd_period_cnt(0),
  access_lid(0ul), access_compute_cycles(0ul)
{}

void
Metadata::updateFromString(string &label, string &s)
{
	if (label == "branchcycle") {
		/* Expecting three numbers: "taken not_taken cycle_pos"
		 * cycle_pos is set to 0 when omitted. */
		if (!read_uint(s, branch_taken))
			throw invalid_argument("Missing 'taken' value");

		if (!read_uint(s, branch_not_taken))
			throw invalid_argument("Missing 'taken' value");

		if (!read_uint(s, branch_cycle_pos_init))
			branch_cycle_pos_init = 0;

		branch_cycle_pos = branch_cycle_pos_init;
	} else {
		throw invalid_argument("Unknown label");
	}
}

Metadata *
Metadata::clone(void)
{
	Metadata *v = new Metadata();

	v->branch_taken = branch_taken;
	v->branch_not_taken = branch_not_taken;
	v->branch_cycle_pos = branch_cycle_pos;
	v->branch_cycle_pos_init = branch_cycle_pos_init;
	v->sd_words = sd_words;
	v->sd_period = sd_period;
	v->sd_period_cnt = sd_period_cnt;

	return v;
}

void
Metadata::setSDConstants(unsigned int w, unsigned int p, unsigned int c)
{
	sd_words = w;
	sd_period = p;
	sd_period_cnt = c;
}

void
Metadata::incrementBranchCycle(void)
{
	/* Modulo 0 undefined in C. */
	if (branch_taken + branch_not_taken == 0)
		return;

	branch_cycle_pos = (branch_cycle_pos + 1) %
			(branch_taken + branch_not_taken);
}

void
Metadata::resetBranchCycle(void)
{
	branch_cycle_pos = branch_cycle_pos_init;
}

void
Metadata::setDRAMlid(unsigned long lid, unsigned long ccycles)
{
	access_lid = lid;
	access_compute_cycles = ccycles;
}

unsigned int
Metadata::getSDWords(void)
{
	return sd_words;
}

unsigned int
Metadata::getSDPeriod(void)
{
	return sd_period;
}

unsigned int
Metadata::getSDPeriodCnt(void)
{
	return sd_period_cnt;
}

unsigned long
Metadata::getDRAMlid(void) const
{
	return access_compute_cycles;
}

bool
Metadata::willBranch(void) const
{
	return branch_cycle_pos < branch_taken;
}

}
