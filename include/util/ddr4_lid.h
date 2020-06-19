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

#ifndef UTIL_DDR4_LID_H
#define UTIL_DDR4_LID_H

#include <cstring>
#include <cinttypes>

#include "util/defaults.h"

namespace dram {

/** Set of DRAM timing parameters.
 *
 * Used to determine the Least-Issue Delay of a contiguous data transfer. */
typedef struct {
	const std::string speed; /**< Speed description string. */
	const std::string org;   /**< DRAM chip organisation string. */

	uint32_t tRCD;  /**< Row-to-Column delay. */
	uint32_t tCAS;  /**< Column Access Strobe. */
	uint32_t tRP;   /**< Row Precharge time. */
	uint32_t tCWD;  /**< Column Write Delay. */
	uint32_t tWR;   /**< Write Recover time. */
	uint32_t tRAS;  /**< Row-Access Strobe. */
	uint32_t tRTP;  /**< Row-to-Precharge delay. */
	uint32_t tRRDs; /**< Row-to-Row Delay, short (diff. bank-group). */
	uint32_t tRRDl; /**< Row-to-Row Delay, long (same bank-group). */
	uint32_t tFAW;  /**< Four-activate window. */
	uint32_t tCCDs; /**< Column-to-Column delay, short (diff. bank-group).*/
	uint32_t tCCDl; /**< Column-to-Column delay, long (same bank-group). */
	uint32_t tRFC;  /**< ReFresh Cycle time.*/
	uint32_t tREFI; /**< REFresh Interval/ */
	uint32_t BL;    /**< Burst length, must be power-of-two */
	uint32_t buswidth_B; /**< Bus width in bytes. */
	uint32_t nBG;   /**< Number of bank-groups. */
	uint32_t clkMHz;     /**< DRAM command clock in MHz. */
} dram_timing;

/** Look up a set of timings for a given speed- and organisation string.
 *
 * Consult src/util/ddr4_lid.cpp for a table of valid combinations.
 * @param speed String describing the speed of the DRAM configuration.
 * @param org String describing the organisation of the DRAM chips.
 * @return A dram_timing object populated with timing information.
 */
const dram_timing *getTiming(const std::string speed, const std::string org,
		uint32_t bg);

/** Determine the number of bursts required for a transfer.
 * @param dram Pointer to set of timing parameters.
 * @param request_length Request size in bytes.
 * @param aligned 1 If this transfer is aligned to the start of a bank-pair.
 * @return Number of bursts required for this transfer.
 */
size_t bursts(const dram_timing *dram, size_t request_length, int aligned);

/** Determine the least issue delay for a read of given bursts.
 * @param dram Pointer to set of timing parameters.
 * @param bursts Number of bursts required.
 * @param aligned True iff this transfer is aligned to the start of a bank-pair.
 * @return The number of cycles required for this transfer.
 */
uint32_t least_issue_delay_rd_ddr4(const dram_timing *dram,
		size_t bursts, int aligned);

/** Determine the least issue delay for a write of given bursts.
 * @param dram Pointer to set of timing parameters.
 * @param bursts Number of bursts required.
 * @param aligned True iff this transfer is aligned to the start of a bank-pair.
 * @return The number of cycles required for this transfer.
 */
uint32_t least_issue_delay_wr_ddr4(const dram_timing *dram, size_t bursts,
		int aligned);

/** Determine the worst-case least issue delay for an index-iterate read
 * operation.
 * @param dram Pointer to set of timing parameters.
 * @param buf_size Size of buffer in # 32-bit words.
 * @param words Number of words to be read (generally number of work-items in a
 *              work-group).
 */
uint32_t
least_issue_delay_idxit_rd_ddr4(const dram_timing *dram, size_t buf_size,
		size_t words);

/** Determine the worst-case least issue delay for an index-iterate write
 * operation.
 * @param dram Pointer to set of timing parameters.
 * @param buf_size Size of buffer in # 32-bit words.
 * @param words Number of words to be read (generally number of work-items in a
 *              work-group).
 */
uint32_t
least_issue_delay_idxit_wr_ddr4(const dram_timing *dram, size_t buf_size,
		size_t words);

/** Determine the time DQ is active for a transfer of given length.
 * @param dram Pointer to set of timing parameters.
 * @param request_length Request size in bytes.
 */
uint32_t data_bus_cycles(const dram_timing *dram, size_t request_length);


/** Inflate a given WCET with the worst-case refresh time.
 *
 * Inflation of WCET equates to a case where refresh occurs in a "drop the
 * world" fashion as soon as required, halting both compute and DRAM. For
 * DDR4-3200AA, inflation under these assumptions unconditionally increases the
 * WCET by ~4.5%. Contrary to this assumption, in the Sim-D pipeline:
 * 1) compute/scratchpads continue to run during a refresh operation,
 * 2) the refresh is deferred until after the current DRAM stride request.
 *
 * Point 2 is reflected in this inflation model as we assume a refresh takes
 * tRFC, and not another worst-case precharge+activate cycle required if we
 * had to "preemptively" execute the refresh. Preemptive refresh is only
 * required for stride patterns that take longer than 8*tREFI, which we only
 * observed with worst index-iterate cases. However, these index-iteration
 * cases assume many points in the worst case where all banks are precharged,
 * hence with minimal cleverness in the DRAM controller even in these cases
 * only tRFC must be paid for a refresh despite performing refresh mid-request.
 *
 * Point 1 implies inflation introduces a pessimism. We accept this pessimism
 * to avoid a more elaborate blocking-time analysis, and leave this for future
 * work.
 *
 * @param dram DRAM timings for the current configuration.
 * @param wcet Old non-inflated worst-case execution time,
 * @return Inflated WCET.
 */
unsigned long inflate_refresh(const dram::dram_timing *dram,
		unsigned long wcet);

}

#endif /* UTIL_DDR4_LID_H */
