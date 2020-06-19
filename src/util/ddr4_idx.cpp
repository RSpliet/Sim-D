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

/** Generate relevant data for the two index iteration schemes in Sim-D. */
int
main(int argc, char **argv)
{
	int c;
	uint32_t lid_cam_2bg,lid_cam_4bg, lid_it_2bg, lid_it_4bg;
	size_t i;
	size_t i_init = 1;
	int aligned = 0;
	size_t req_length = 0;
	size_t b2, b4;
	const dram_timing *t2, *t4;

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

	if (req_length <= 0) {
		req_length = 32768;
	}

	cout << "Least issue delay - excluding refresh cycles!" << endl << endl;

	t2 = getTiming("DDR4_3200AA","DDR4_8Gb_x16", 2);
	t4 = getTiming("DDR4_3200AA","DDR4_8Gb_x8", 4);

	printf("\"Buffer size (B)\", \"Snoopy indexed read (2 bank-groups)\", \"Snoopy indexed read (4 bank-groups)\", \"Iterative indexed read (2 bank-groups)\", \"Iterative indexed read (4 bank-groups)\", \"Snoopy indexed write (2 bank-groups)\", \"Snoopy indexed write (4 bank-groups)\", \"Iterative indexed write (2 bank-groups)\", \"Iterative indexed write (4 bank-groups)\"\n");
	for (i = i_init; i < req_length; i++) {
		b2 = bursts(t2, i * 4, aligned);
		b4 = bursts(t4, i * 4, aligned); /* Should be equal. */
		lid_cam_2bg = least_issue_delay_rd_ddr4(t2, b2, aligned);
		lid_cam_4bg = least_issue_delay_rd_ddr4(t4, b4, aligned);
		lid_it_2bg = least_issue_delay_idxit_rd_ddr4(t2, i, COMPUTE_THREADS);
		lid_it_4bg = least_issue_delay_idxit_rd_ddr4(t4, i, COMPUTE_THREADS);
		printf("%7zu, %6u, %6u, %6u, %6u", i * 4, lid_cam_2bg, lid_cam_4bg, lid_it_2bg, lid_it_4bg);

		lid_cam_2bg = least_issue_delay_wr_ddr4(t2, b2, aligned);
		lid_cam_4bg = least_issue_delay_wr_ddr4(t4, b4, aligned);
		lid_it_2bg = least_issue_delay_idxit_wr_ddr4(t2, i, COMPUTE_THREADS);
		lid_it_4bg = least_issue_delay_idxit_wr_ddr4(t4, i, COMPUTE_THREADS);
		printf("%6u, %6u, %6u, %6u\n", lid_cam_2bg, lid_cam_4bg, lid_it_2bg, lid_it_4bg);
	}
	cout << endl;

	return 0;
}
