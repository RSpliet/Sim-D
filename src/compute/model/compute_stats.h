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

#ifndef COMPUTE_MODEL_COMPUTE_STATS_H
#define COMPUTE_MODEL_COMPUTE_STATS_H

#include <ostream>
#include <iomanip>

#include "isa/model/Instruction.h"

using namespace std;

namespace compute_model {

/**
 * Object containing performance counter values for a given compute simulation.
 */
class compute_stats {
public:
	/** Total execution time of last kernel in cycles. */
	unsigned long exec_time;

	/** Number of cycles spent loading the program. */
	uint32_t prg_load_time;

	/** Number of threads launched. */
	unsigned long threads;

	/** Number of workgroups launched. */
	unsigned long wgs;

	/** Maximum number of scoreboard entries. */
	unsigned int max_scoreboard_entries;


	unsigned long dram_active; /**< Number of active cycles of DRAM */
	unsigned long compute_active; /**< Number of cycles compute was
				       * active. */
	unsigned long sp_active[2]; /**< Cycles each SP is active. */
	unsigned long raw_stalls; /**< Number of RAW stall cycles. */
	unsigned long rf_bank_conflict_stalls; /**< Number of stall cycles
						* caused by regfile bank
						* conflicts */
	unsigned long resource_busy_stalls; /**< Number of stall cycles caused
					     * by resources (eg. SIDIV unit)
					     * being occupied. */

	/** Number of words read from the VRF through the DRAM interface. */
	unsigned long dram_vrf_words_r;
	/** Number of words written to the VRF through the DRAM interface. */
	unsigned long dram_vrf_words_w;

	/** Net number of bytes read from the VRF through the DRAM interface. */
	unsigned long dram_vrf_net_words_r;
	/** Net number of bytes written to the VRF through the DRAM interface.*/
	unsigned long dram_vrf_net_words_w;

	/********* IExecute. Committed instruction count **************/
	unsigned long commit_vec[isa_model::CAT_SENTINEL]; /**< \#Vector sub-insns. */
	unsigned long commit_sc[isa_model::CAT_SENTINEL];  /**< \#Scalar insns.*/
	unsigned long commit_nop;		/**< \#NOPs, pipeline bubbles. */

	/** SystemC mandatory print stream operation.
	 * @param os Output stream
	 * @param stats Reference to statistics object to print.
	 * Return Output stream */
	inline friend ostream &
	operator<<(ostream &os, compute_stats const &stats)
	{
		unsigned int i;
		double compute_util = std::numeric_limits<double>::quiet_NaN();
		double dram_util = std::numeric_limits<double>::quiet_NaN();
		double sp0_util = std::numeric_limits<double>::quiet_NaN();
		double sp1_util = std::numeric_limits<double>::quiet_NaN();

		unsigned long commit_vec = 0ul;
		unsigned long commit_vec_ops = 0ul;
		unsigned long commit_sc = 0ul;

		unsigned long ops;
		double ops_cycle;

		if (stats.exec_time) {
			compute_util = ((double) stats.compute_active * 100) / ((double) stats.exec_time);
			dram_util = ((double) stats.dram_active * 100) / ((double) stats.exec_time);
			sp0_util = ((double) stats.sp_active[0] * 100) / ((double) stats.exec_time);
			sp1_util = ((double) stats.sp_active[1] * 100) / ((double) stats.exec_time);
		}

		for (i = 0; i < isa_model::CAT_SENTINEL; i++) {
			commit_vec += stats.commit_vec[i];
			commit_sc += stats.commit_sc[i];
			if (i == isa_model::CAT_ARITH_RCPU)
				commit_vec_ops += stats.commit_vec[i] * COMPUTE_RCPUS;
			else
				commit_vec_ops += stats.commit_vec[i] * COMPUTE_FPUS;
		}

		ops = commit_vec_ops + commit_sc;
		ops_cycle = ((double)ops) / ((double)stats.exec_time);

		os << "=== Compute stats ===" << endl;
		os << "Program latency            :" << setw(10) << stats.exec_time << endl;
		os << "Program load time          :" << setw(10) << stats.prg_load_time << endl;
		os << "# Threads                  :" << setw(10) << stats.threads << endl;
		os << "# Work-groups              :" << setw(10) << stats.wgs << endl;
		os << "# scoreboard entries (max) :" << setw(10) << stats.max_scoreboard_entries << endl;
		os << "DRAM active (compute cycs) :" << setw(10) << stats.dram_active << " (" << dram_util << "%)" << endl;
		os << "SP0 active (compute cycs)  :" << setw(10) << stats.sp_active[0] << " (" << sp0_util << "%)" << endl;
		os << "SP1 active (compute cycs)  :" << setw(10) << stats.sp_active[1] << " (" << sp1_util << "%)" << endl;
		os << "Compute active cycles      :" << setw(10) << stats.compute_active << " (" << compute_util << "%)" << endl;
		os << endl;
		os << "= Performance counters - commit stage" << endl;
		os << "Vector (sub-)instructions                                      :" << setw(10) << commit_vec << endl;
		os << "Vector ops                                                     :" << setw(10) << commit_vec_ops << endl;
		for (i = 0; i < isa_model::CAT_SENTINEL; i++)
			os << "   " << setw(45) << left << isa_model::cat_str[i] << " :" << setw(10) << right << stats.commit_vec[i] << endl;
		os << "Scalar instructions/ops                                        :" << setw(10) << commit_sc << endl;
		for (i = 0; i < isa_model::CAT_SENTINEL; i++)
			os << "   " << setw(45) << left << isa_model::cat_str[i] << " :" << setw(10) << right << stats.commit_sc[i] << endl;
		os << "NOPs/Pipeline bubbles                                          :" << setw(10) << stats.commit_nop << endl;
		os << endl;
		os << "Net Ops/cycle (== GOPS)    :" << setw(10) << ops_cycle << endl;
		os << endl;
		os << "= Stall counters" << endl;
		os << "RAW stall cycles           :" << setw(10) << stats.raw_stalls << endl;
		os << "RF bank conflict stall cycs:" << setw(10) << stats.rf_bank_conflict_stalls << endl;
		os << "Blocked SIDIV stall cycs   :" << setw(10) << stats.resource_busy_stalls << endl;
		os << endl;
		os << "= VRF<->DRAM interface" << endl;
		os << "VRF net read words         :" << setw(10) << stats.dram_vrf_net_words_r << endl;
		os << "VRF net written words      :" << setw(10) << stats.dram_vrf_net_words_w << endl;
		os << "VRF bank words read        :" << setw(10) << stats.dram_vrf_words_r << endl;
		os << "VRF bank words written     :" << setw(10) << stats.dram_vrf_words_w << endl;

		return os;
	}
};

}

#endif /* COMPUTE_MODEL_COMPUTE_STATS_H */
