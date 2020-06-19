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

#ifndef MC_MODEL_CMDARB_STATS_H
#define MC_MODEL_CMDARB_STATS_H

#include <ostream>
#include <iomanip>

using namespace std;

namespace mc_model {

/** Object containing performance counter and power estimate values for a given
 * memory controller simulation. These values are generated as the command
 * arbiter issues new commands.
 */
class cmdarb_stats {
public:
	/** Base address, used for the "mc" tool to visualise latency for
	 * a given alignment. */
	unsigned long base_addr;
	/** Least-issue delay */
	long lid;
	/** Last data arrival */
	unsigned long lda;
	/** Number of activate commands */
	unsigned int act_c;
	/** Number of precharge commands */
	unsigned int pre_c;
	/** Number of CAS operations (read/write) */
	unsigned int cas_c;
	/** Number of refresh operations */
	unsigned int ref_c;

	/** Number of bytes transferred in total. */
	unsigned long bytes;

	/** Utilisation */
	double dq_util;

	/** Total energy consumed (in picojoules) */
	double energy;
	/** Average power consumption (miliwatts) */
	double power;

	/** SystemC mandatory print stream operation */
	inline friend std::ostream&
	operator<<( std::ostream& os, cmdarb_stats const & stats )
	{
		os << "=== Stats (Base addr: 0x" << hex << stats.base_addr << dec << ") ===" << endl;
		os << "Bytes transferred    : " << setw(10) << stats.bytes << " (" << stats.dq_util << "%)" << endl;
		os << "Latest data arrival  : " << setw(10) << stats.lda << endl;
		os << "Least-issue delay    : " << setw(10) << stats.lid << endl;
		os << "# Read/write ops     : " << setw(10) << stats.cas_c << endl;
		os << "# Activate ops       : " << setw(10) << stats.act_c << endl;
		os << "# Explicit PRE ops   : " << setw(10) << stats.pre_c << endl;
		os << "# Refresh ops        : " << setw(10) << stats.ref_c << endl;

		os << "Total energy (pJ)    : " << setw(10) << stats.energy << endl;
		os << "Average power (mW)   : " << setw(10) << stats.power << endl;

		return os;
	}

	/** Make this object contain the minimum values of this and s. */
	void min(cmdarb_stats &s);
	/** Make this object contain the maximum values of this and s. */
	void max(cmdarb_stats &s);
	/** Aggregate provided statistics into this object. */
	void aggregate(cmdarb_stats &s);
};

}

#endif /* MC_MODEL_CMDARB_STATS_H */
