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

/** Simple stride generation tool that both helps understand the accesses
 * performed by our Sim-D stride, as gives some quick figures on the 
 * performance of PRET */

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <inttypes.h>
#include <cstring>
#include <cstdarg>
#include <iomanip>
#include <unistd.h>
#include <algorithm>

#include "util/defaults.h"

using namespace std;

enum {
	DEBUG_OUTPUT_STRIDE = 0,
	DEBUG_OUTPUT_STATS,
	DEBUG_OUTPUT_SENTINEL
};

static bool debug_output[DEBUG_OUTPUT_SENTINEL] = {0};

void printf_stride(const char *fmt, ...) {
	if (debug_output[DEBUG_OUTPUT_STRIDE]) {
		va_list args;
		va_start(args, fmt);
		vprintf(fmt, args);
		va_end(args);
	}
}

/* Observations
 * - For some patterns with even stride, adjacent word lanes have different
 *   patterns (eg. 11 22)
 * - Cycle length will never be shorter than stride. For
 *   n = BUSWIDTH*BURST/WORD_LENGTH word lanes, we don't shift for more than
 *   2log(n)
 * - Always: byte lane pattern:
 *      repeat several times (q+1 times X, undefined times .),
 *      (1 times X, undefined times .)
 * - For per-bytelane parallel determinism get rid of modulo op?
	For stride >= #word lanes: YES. Overflow results in single subtraction.
	So requirement is add+sub. What if stride < # word lanes: YES. Scale
	addition down so we always end up with 1* subtract.

		For POT strides, don't bother adding anything at all
		---8 word-lanes.---
		(5, 6, 7): 1 or 2. 
		3: 1, 2, 3
		
		Scale addition. Small LUT with <#word lanes> mod <stride>
		values: 8*3. for 8 byte lanes (0, 0, 2, 0, 3, 2, 1, 0). No skip
		logic required.
 * This means generating up to 1 command per cycle. Acceptable if word/stride
 * ratio > 0.25 (4 cycles per read op). Can we cleverly skip without an
 * expensive modulo?
 *	YES! See code at comment labelled SKIP. Last word idx will have
 *	additional logic attached.
 * - Bytelane patterns easier to describe than channel patterns. see (11 22).
 * - (11 21) has sub-patterns... 8 long, high 1, 2, 4, 7
 
 * This is a lot of operations. It's short adds and subtracts though (17 bit
 * max). At 1.2GHz, we can do quite a few of them on the critical path? 4?
 */

unsigned int
do_generate(unsigned int wordsize, unsigned int stride, unsigned int cycle,
	unsigned int sp_bus_width)
{
	unsigned int pos;
	uint64_t i;
	unsigned int have_skipped = 0;
	unsigned int skip;
	unsigned int skipcnt;
	unsigned int skiprst;
	uint64_t last_addr = 0xffffffffffffffffull;
	uint64_t addr;
	unsigned int reads = 0;
	
	skipcnt = (stride - (wordsize + sp_bus_width - 1));
	skiprst = (skipcnt & (sp_bus_width - 1));
	
	skipcnt &= ~(sp_bus_width - 1);

	for (i = 0; i < cycle; i++) {
		/* Modulo expensive, in HW use single subtraction instead */
		pos = i % stride;
		
		if (i % sp_bus_width == 0) {
			printf_stride(" : ");
			reads++;
			
			/* Determine groups */
			addr = i << 2ull;
			
			last_addr = i << 2ull;
		}
		
		if (pos < wordsize) {
			have_skipped = 0;
			printf_stride("X ");
		} else {
			printf_stride(". ");
		}
		
		if (i % 4 == 3)
			printf_stride(" ");
		
		if (i % sp_bus_width == sp_bus_width - 1) {
			printf_stride("\n");
			
			/* SKIP */
			if (!have_skipped) {
				skip = 0;
				if (pos >= wordsize-1) {
					skip = skipcnt;
					if (pos < wordsize+skiprst - 1)
						skip += sp_bus_width;
				}
				if (!skip)
					continue;
				
				printf_stride("--- skip 0x%04x ---\n", skip << 2);
				i += skip;
				have_skipped = 1;
			}
		}
	}
	printf_stride("\n");

	return reads;
}

void
help(char *bin_name)
{
	printf("\n");
	printf("Usage: %s [options] <words> <period>\n", bin_name);
	printf("       %s -g [options] <words>\n", bin_name);
	printf("Options:\n");
	printf("\t-c [num]: Period count.\n");
	printf("\t-s      : Output Predator per-burstcount summary.\n");
	printf("\t-S      : Output graphical stride pattern.\n");
	printf("\t-g      : Output gnuplot-friendly least-issue delay for "
			"[1-<words>].\n");
}

void
parse_parameters(int argc, char* argv[], unsigned int *wordsize,
		unsigned int *stride, unsigned int *cycle)
{
	int c;
	int minargs = 3;

	if (argc < minargs) {
		help(argv[0]);
		return exit(1);
	}

	if (minargs == 3) {
		*wordsize = strtoul(argv[argc-2], NULL, 10);
		*stride = strtoul(argv[argc-1], NULL, 10);
	} else {
		*wordsize = strtoul(argv[argc-1], NULL, 10);
		*stride = *wordsize;
	}

	if (*stride == 0 || *wordsize == 0) {
		printf("Error: parameters cannot be 0\n");
		help(argv[0]);
		exit(1);
	}

	if (*stride < *wordsize) {
		printf("Error: stride cannot be smaller than word size\n");
		help(argv[0]);
		exit(1);
	}

	/* default for cycle */
	*cycle = 33 * (*stride);

	if (*stride & 0x1) {
		/* do nothing **/
	} else if (*stride & 0x2) {
		*cycle >>= 1;
	} else if (*stride & 0x4) {
		*cycle >>= 2;
	} else if (*stride & 0x8) {
		*cycle >>= 3;
	} else {
		*cycle >>= 4;
	}

	/* Take stride patterns from the command line */
	while ( (c = getopt(argc+1-minargs, argv, "c:sSg")) != -1) {
		switch (c) {
		case 'c':
			*cycle = (strtoul(optarg, NULL, 10)-1) * (*stride) + (*wordsize);
			break;
		case 's':
			debug_output[DEBUG_OUTPUT_STATS] = 1;
			break;
		case 'S':
			debug_output[DEBUG_OUTPUT_STRIDE] = 1;
			break;
		default:
			help(argv[0]);
			exit(1);
		}
	}

	if (!debug_output[DEBUG_OUTPUT_STATS] &&
			!debug_output[DEBUG_OUTPUT_STRIDE]) {
		printf("Error: no output format selected\n");
		help(argv[0]);
		exit(1);
	}

}

int main(int argc, char **argv)
{
	unsigned int wordsize;
	unsigned int stride;
	unsigned int reads;
	unsigned int cycle;

	parse_parameters(argc, argv, &wordsize, &stride, &cycle);

	reads = do_generate(wordsize, stride, cycle, SP_BUS_WIDTH);

	if (debug_output[DEBUG_OUTPUT_STATS])
		printf("Reads: %u\n", reads);
}
