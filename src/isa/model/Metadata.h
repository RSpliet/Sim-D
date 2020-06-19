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

#ifndef ISA_MODEL_METADATA_H
#define ISA_MODEL_METADATA_H

#include <string>
#include <iostream>

namespace isa_model {

/** Metadata associated with an instruction. Used for WCET derivation.
 *
 * This class conveys two things:
 * - Branchcycle annotations (and state),
 * - DRAM/scratchpad access parameters/times.
 *
 * Branchcycles are user-provided annotations for a "sicj" (scalar integer
 * conditional jump) instructions. They look like e.g.:
 * \#bound branchcycle 9,1,0
 * This branchcycle describes a cyclical branch decision, where 9 out of 10
 * times that this instruction is encountered during run-time, the branch will
 * be taken. 1 out of 10 times this branch is not taken. The cycle starts at 0,
 * meaning the first nine branches will be taken, follwed by a single non-taken
 * branch. */
class Metadata {
private:
	/** Number of times in a branchcycle the branch will be taken. */
	unsigned int branch_taken;
	/** Number of times in a branchcycle the branch will not be taken. */
	unsigned int branch_not_taken;
	/** The current position in the branchcycle (state). */
	unsigned int branch_cycle_pos;
	/** The initial position in the branchcycle (user-provided). */
	unsigned int branch_cycle_pos_init;

	/** Stride descriptor: # words/period */
	unsigned int sd_words;
	/** Stride descriptor: length of a period in words. */
	unsigned int sd_period;
	/** Stride descriptor: Number of periods. */
	unsigned int sd_period_cnt;

	/** Simulated/computed access time (longest-issue delay) In DRAM command
	 * bus cycles.*/
	unsigned long access_lid;
	/** Longest-issue delay, translated to compute cycles. */
	unsigned long access_compute_cycles;

public:
	/** Default constructor */
	Metadata();

	/** Update the metadata from a user-provided string.
	 * @param label The label of the annotation ("branchcycle")
	 * @param s The string following this label containing the parameters.*/
	void updateFromString(std::string &label, std::string &s);

	/** Clone this metadata object.
	 * @return A cloned newly allocated object. */
	Metadata* clone(void);

	/** Update the stride descriptor values for this Instruction.
	 * @param w Words
	 * @param p Period
	 * @param c Number of periods. */
	void setSDConstants(unsigned int w, unsigned int p, unsigned int c);

	/** Reset the branch cycle back to its init value. */
	void resetBranchCycle(void);
	/** Increment the branch cycle position by one. */
	void incrementBranchCycle(void);

	/** Set the LID of a DRAM operation
	 * @param lid LID in DRAM command bus cycles.
	 * @param ccycles LID in compute cycles. */
	void setDRAMlid(unsigned long lid, unsigned long ccycles);

	/** Return the number of words in the stride descriptor.
	 * @return The number of words in the stride descriptor. */
	unsigned int getSDWords(void);
	/** Return the period length in the stride descriptor.
	 * @return The period length in the stride descriptor. */
	unsigned int getSDPeriod(void);
	/** Return the number of periods in the stride descriptor.
	 * @return The number of periods in the stride descriptor. */
	unsigned int getSDPeriodCnt(void);

	/** Return the DRAM LID in compute cycles.
	 * @return The number of compute cycles the associated DRAM operation
	 * 	   takes. */
	unsigned long getDRAMlid(void) const;

	/** Return true iff this instruction will branch according to the
	 * branchcycle.
	 * @return True iff this instruction will branch. */
	bool willBranch(void) const;

	/** SystemC mandatory print stream operation.
	 * @param os Output stream.
	 * @param v Metadata to print.
	 * @return Output stream. */
	inline friend std::ostream &
	operator<<(std::ostream &os, Metadata const &v)
	{
		os << "Meta: branchcycle(" << v.branch_taken << ","
			<< v.branch_not_taken << "," << v.branch_cycle_pos
			<< ") sd(" << v.sd_words << "," << v.sd_period << ","
			<< v.sd_period_cnt << ")";

		if (v.access_lid)
			os << " DRAM(" << v.access_lid << " @ DRAM clk, " <<
			v.access_compute_cycles << " @ compute clk)";

		return os;
	}
};

}

#endif /* ISA_MODEL_METADATA_H */
