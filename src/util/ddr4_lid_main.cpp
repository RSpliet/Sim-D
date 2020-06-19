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

#include <iostream>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <unistd.h>
#include <cinttypes>
#include <util/ddr4_lid.h>

using namespace std;
using namespace dram;

int
main(int argc, char **argv)
{
	int c;
	uint32_t lid_rd, lid_wr;
	size_t i;
	size_t i_init;
	uint32_t u;
	float u_rd;
	float u_wr;
	int aligned = 1;
	size_t req_length = 0;
	size_t b;
	const dram_timing *t;

	while ((c = getopt(argc, argv, "us:")) != -1)
	{
		switch (c) {
		case 'u':
			aligned = 0;
			break;
		case 's':
			req_length = atoll(optarg);
			break;
		default:
			cout << "Unknown option: -" <<  c;
			return -1;
		}
	}


	/* Prints #bursts rather than transfer width for easier comparison
	 * with narrower DDR2 buses in literature */

	if (req_length > 0) {
		i_init = req_length;
	} else {
		i_init = 64;
		req_length = 32768;
	}

	cout << "Least issue delay - excluding refresh cycles!" << endl << endl;

	cout << "Micron DDR4 3200AA 2-bank groups (8 banks):" << endl;
	cout << "Bytes    Cyc RD    Util\% Cyc WR    Util\%" << endl;

	t = getTiming("DDR4_3200AA","DDR4_8Gb_x16", 2);

	for (i = i_init; i < req_length + 1; i <<= 1) {
		b = bursts(t, i, aligned);
		lid_rd = least_issue_delay_rd_ddr4(t, b, aligned);
		lid_wr = least_issue_delay_wr_ddr4(t, b, aligned);
		u = data_bus_cycles(t, i);
		u_rd = (float)u / (float)lid_rd;
		u_wr = (float)u / (float)lid_wr;
		printf("%7zu: %6u %f %6u %f\n", i, lid_rd, u_rd, lid_wr, u_wr);
	}
	cout << endl;

	cout << "Micron DDR4 3200AA 4-bank groups (16 banks):" << endl;
	cout << "Bytes    Cyc RD    Util\% Cyc WR    Util\%" << endl;

	t = getTiming("DDR4_3200AA","DDR4_8Gb_x8", 4);

	for (i = i_init; i < req_length + 1; i <<= 1) {
		b = bursts(t, i, aligned);
		lid_rd = least_issue_delay_rd_ddr4(t, b, aligned);
		lid_wr = least_issue_delay_wr_ddr4(t, b, aligned);
		u = data_bus_cycles(t, i);
		u_rd = (float)u / (float)lid_rd;
		u_wr = (float)u / (float)lid_wr;
		printf("%7zu: %6u %f %6u %f\n", i, lid_rd, u_rd, lid_wr, u_wr);
	}

	return 0;
}
