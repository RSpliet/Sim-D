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

#include <algorithm>
#include <iostream>

#include "util/ddr4_lid.h"

using namespace std;

namespace dram {

/** @todo we're linking with Ramulator, which has all this info already. A
 * method that generates a dram_timing struct from a ramulator::DDR4 struct
 * (or convert the ddr4_lid methods to work directly on the DDR4 struct) would
 * reduce the potential for human error and reduce manual labour burden.
 * @todo This code was extracted from an early C prototype. Could probably do
 * with being a proper object. */
const dram_timing micron_3200aa_2bg = {
	.speed = "DDR4_3200AA",
	.org = "DDR4_8Gb_x16",
	.tRCD = 22,
	.tCAS = 22,
	.tRP = 22,
	.tCWD = 16,
	.tWR = 24,
	.tRAS = 52,
	.tRTP = 12,
	.tRRDs = 9,
	.tRRDl = 11,
	.tFAW = 48,
	.tCCDs = 4,
	.tCCDl = 8,
	.tRFC = 560,
	.tREFI = 12480,
	.BL = 8,
	.buswidth_B = 8,
	.nBG = 2,
	.clkMHz = 1600
};

const dram_timing micron_3200aa_4bg = {
	.speed = "DDR4_3200AA",
	.org = "DDR4_8Gb_x8",
	.tRCD = 22,
	.tCAS = 22,
	.tRP = 22,
	.tCWD = 16,
	.tWR = 24,
	.tRAS = 52,
	.tRTP = 12,
	.tRRDs = 4,
	.tRRDl = 8,
	.tFAW = 34,
	.tCCDs = 4,
	.tCCDl = 8,
	.tRFC = 560,
	.tREFI = 12480,
	.BL = 8,
	.buswidth_B = 8,
	.nBG = 4,
	.clkMHz = 1600
};

const dram_timing ramulator_1866m_2bg = {
	.speed = "DDR4_1866M",
	.org = "DDR4_8Gb_x16",
	.tRCD = 13,
	.tCAS = 13,
	.tRP = 13,
	.tCWD = 10,
	.tWR = 14,
	.tRAS = 32,
	.tRTP = 7,
	.tRRDs = 5,
	.tRRDl = 6,
	.tFAW = 28,
	.tCCDs = 4,
	.tCCDl = 5,
	.tRFC = 327,
	.tREFI = 7280,
	.BL = 8,
	.buswidth_B = 8,
	.nBG = 2,
	.clkMHz = 933,
};

const static dram_timing *timing_map[] = {
	&ramulator_1866m_2bg,
	&micron_3200aa_2bg,
	&micron_3200aa_4bg,
};

const dram_timing *
getTiming(const string speed, const string org, uint32_t bg)
{
	for (auto e : timing_map) {
		if (e->speed == speed && e->org == org && e->nBG == bg)
			return e;
	}

	cerr << "Error: DRAM organisation unsupported by least-issue delay "
			"calcuator. Please add an entry to "
			"src/util/ddr4_lid.cpp and rebuild Sim-D." << endl;

	exit(1);

	return nullptr;
}

uint32_t
rrd_penalty(int n, const dram_timing *dram)
{
	uint32_t penalty = 0;
	unsigned int t = dram->tRCD;

	for (int i = 0; i < n - 2; i++) {
		if (t > (3 * dram->tRRDs) + penalty)
			break;

		if (t % dram->tRRDs == penalty) {
			penalty++;
		}

		t += (i == 0) ? max(dram->tRRDs, (uint32_t) 4): 4;
	}

	return penalty;
}

static uint32_t
tACTCAS_ddr4(const dram_timing *dram, size_t bursts)
{
	uint32_t l;

	if (dram->nBG > 2) {
		l = dram->tRCD + ((bursts - 1) * dram->tCCDs);
	} else {
		switch (bursts) {
		case 1:
		case 2:
		case 3:
		case 4:
			l = ((bursts - 1) * dram->tRRDs) + dram->tRCD;
			break;
		case 5:
		case 6:
			l = 2 * dram->tRRDs + dram->tRCD + ((bursts - 4) * dram->tCCDl) + dram->tCCDs;
			break;
		case 7:
		case 8:
			l = 3 * dram->tRRDs + dram->tRCD + dram->tCCDl + dram->tCCDs;
			break;
		default:
			if (bursts & 0x1) /* Odd */
				l = dram->tRRDs + dram->tRCD + (2 * dram->tCCDl) + ((bursts - 4) * dram->tCCDs);
			else /* Even */
				l = (2*dram->tRRDs) + dram->tRCD + dram->tCCDl + ((bursts - 5) * dram->tCCDs);
			break;
		}
	}

	return l;
}

size_t
bursts(const dram_timing *dram, size_t request_length, int aligned)
{
	size_t bursts = (request_length + (dram->BL * dram->buswidth_B) - 8) / (dram->BL * dram->buswidth_B);

	if (!aligned)
		bursts++;

	return bursts;
}

uint32_t
least_issue_delay_rd_ddr4(const dram_timing *dram, size_t bursts, int aligned)
{
	uint32_t lid;
	uint32_t rp_delay;
	uint32_t rtp_delay = 0;
	uint32_t rrd2ras_delay = 0;

	if (bursts > 1) {
		rtp_delay += max((size_t) 0, min(bursts - 1,  (size_t) (aligned ? 1 : 3)) * (dram->tRRDs - (dram->BL/2)));
		rrd2ras_delay = min(bursts - 1, (aligned ? (size_t) 1 : (size_t) 3)) * dram->tRRDs;
	}

	rtp_delay = tACTCAS_ddr4(dram, bursts) + dram->tRTP;
	/* rtp_delay -= dram->tCCD; Memory Systems: Cache, DRAM, Disk - Table 15.8 p577, seems inaccurate */
	rp_delay = max(rtp_delay, dram->tRAS + rrd2ras_delay);
	lid = rp_delay + dram->tRP;

	return lid + 3;
}

uint32_t
least_issue_delay_wr_ddr4(const dram_timing *dram, size_t bursts, int aligned)
{
	uint32_t lid;
	uint32_t wtp_delay = 0;
	uint32_t rrd2ras_delay = 0;

	if (bursts > 1) {
		wtp_delay += max((size_t) 0, min(bursts - 1, aligned ? (size_t) 1 : (size_t) 3) * (dram->tRRDs - (dram->BL/2)));
		rrd2ras_delay = min(bursts - 1, (size_t) 3) * dram->tRRDs;
	}

	wtp_delay = tACTCAS_ddr4(dram, bursts) + dram->tCWD + (dram->BL/2) + dram->tWR;
	lid = wtp_delay + dram->tRP;

	return lid + 3;
}

static uint32_t
tIIACTCAS_ddr4(const dram_timing *dram, size_t words, unsigned int rows)
{
	unsigned long l;

	if (rows > dram->nBG && dram->tRRDl > dram->tCCDl)
		/* XXX: unclear what this would do if nBG > 2. Irrelevant in
		 * practice as tRRDl < tCCDl for the modelled DRAM
		 * configurations. */
		l = dram->tRCD + dram->tRRDl + dram->tRRDs - 1 +
				(words-3) * dram->tCCDl;
	else if (rows > 1 && dram->tRRDs > dram->tCCDl)
		l = dram->tRCD + dram->tRRDs + (words-2) * dram->tCCDl;
	else
		l = dram->tRCD + (words-1) * dram->tCCDl;

	return l;
}

/** Estimate the number of rows spanned by a buffer of given size.
 * @param dram Pointer to DRAM timings.
 * @param buf_size total size of the buffer.
 * @return Number of rows spanned by the buffer in the worst case.
 * XXX: This is a conservative estimate, that can be tightened by taking into
 * account buffer alignment. */
static unsigned int
size_to_rows(const dram_timing *dram, size_t buf_size)
{
	unsigned int rows;
	unsigned int bank_words;
	unsigned int burst_words;

	bank_words = MC_DRAM_COLS * 2; /* 64-bit bus, two words per col. */
	burst_words = (dram->BL * dram->buswidth_B / 4);

	if (buf_size == 1)
		rows = 1;
	else if (buf_size <= burst_words + 1)
		rows = 2;
	else
		rows = 2 + div_round_up(buf_size - (burst_words + 1),
				bank_words);

	return rows;
}

uint32_t
least_issue_delay_idxit_rd_ddr4(const dram_timing *dram, size_t buf_size,
		size_t words)
{
	unsigned int rows;
	unsigned long bound;

	rows = size_to_rows(dram, buf_size);

	if (rows > dram->nBG * 4) /* XXX: hard-coded 4 banks per group */
		bound = words * (dram->tRAS + dram->tRP);
	else
		bound = tIIACTCAS_ddr4(dram, words, rows) + dram->tRTP
				+ dram->tRP;

	return bound;
}

uint32_t
least_issue_delay_idxit_wr_ddr4(const dram_timing *dram, size_t buf_size,
		size_t words)
{
	unsigned int rows;
	unsigned long bound;

	rows = size_to_rows(dram, buf_size);

	if (rows > dram->nBG * 4) /* XXX: hard-coded 4 banks per group */
		bound = words * (dram->tRCD + dram->tCWD + (dram->BL/2) +
					dram->tWR + dram->tRP);
	else
		bound = tIIACTCAS_ddr4(dram, words, rows) + dram->tCWD +
					(dram->BL/2) + dram->tWR + dram->tRP;

	return bound;
}

uint32_t
data_bus_cycles(const dram_timing *dram, size_t request_length)
{
	size_t bursts = request_length / (dram->BL * dram->buswidth_B);

	if (request_length & (dram->BL - 1))
		bursts++;

	return bursts * dram->BL / 2;
}

unsigned long
inflate_refresh(const dram_timing *dram, unsigned long cycles)
{
	unsigned long refresh_count;
	unsigned long tNOREFI;

	/* In compute cycles. */
	tNOREFI = ((dram->tREFI - dram->tRFC) * 1000) / dram->clkMHz;

	refresh_count = div_round_up(cycles, tNOREFI);
	cycles += div_round_up(refresh_count * dram->tRFC * 1000, dram->clkMHz);

	return cycles;
}

}
