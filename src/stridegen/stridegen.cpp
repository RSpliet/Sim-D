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
#include <iomanip>
#include <unistd.h>
#include <algorithm>

#include <libdrampower/LibDRAMPower.h>
#include <xmlparser/MemSpecParser.h>

using namespace std;

enum {
	DEBUG_OUTPUT_STRIDE = 0,
	DEBUG_OUTPUT_STATS,
	DEBUG_OUTPUT_LID_GNUPLOT,
	DEBUG_OUTPUT_STATS_LATEX,
	DEBUG_OUTPUT_SENTINEL
};

static bool debug_output[DEBUG_OUTPUT_SENTINEL] = {0};

typedef struct {
	unsigned long cycle;
	unsigned int bank;
	Data::MemCommand::cmds cmd;
	bool last;
} pret_ptrn;

typedef struct {
	unsigned long latency;
	unsigned long act;
	unsigned long cas;
	unsigned long pre;
} pret_ptrn_stats;

static libDRAMPower *pwr[9]; /* One per read ptrn size */
static pret_ptrn_stats stats[9];
static Data::MemorySpecification *memSpec;

pret_ptrn_stats ptrn_3200_stats[9] = {
	{.latency = 74,   .act = 1, .cas = 1,   .pre = 0},
	{.latency = 74,   .act = 1, .cas = 2,   .pre = 0},
	{.latency = 74,   .act = 2, .cas = 4,   .pre = 0},
	{.latency = 74,   .act = 4, .cas = 8,   .pre = 0},
	{.latency = 88,   .act = 4, .cas = 16,  .pre = 0},
	{.latency = 152,  .act = 4, .cas = 32,  .pre = 0},
	{.latency = 280,  .act = 4, .cas = 64,  .pre = 0},
	{.latency = 536,  .act = 4, .cas = 128, .pre = 0},
	{.latency = 1048, .act = 4, .cas = 256, .pre = 0},
};

pret_ptrn_stats ptrn_2400_stats[9] = {
	{.latency = 57,   .act = 1, .cas = 1,   .pre = 0},
	{.latency = 57,   .act = 1, .cas = 2,   .pre = 0},
	{.latency = 57,   .act = 2, .cas = 4,   .pre = 0},
	{.latency = 57,   .act = 4, .cas = 8,   .pre = 0},
	{.latency = 82,   .act = 4, .cas = 16,  .pre = 0},
	{.latency = 146,  .act = 4, .cas = 32,  .pre = 0},
	{.latency = 274,  .act = 4, .cas = 64,  .pre = 0},
	{.latency = 530,  .act = 4, .cas = 128, .pre = 0},
	{.latency = 1042, .act = 4, .cas = 256, .pre = 0},
};

pret_ptrn ptrn_3200[9][260] = {
	{
		{.cycle = 0, .bank = 0, .cmd = Data::MemCommand::ACT},
		{.cycle = 40, .bank = 0, .cmd = Data::MemCommand::RDA, .last = 1}
	},
	{
		{.cycle = 0, .bank = 0, .cmd = Data::MemCommand::ACT},
		{.cycle = 22, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 40, .bank = 0, .cmd = Data::MemCommand::RDA, .last = 1}
	},
	{
		{.cycle = 0, .bank = 0, .cmd = Data::MemCommand::ACT},
		{.cycle = 9, .bank = 1, .cmd = Data::MemCommand::ACT},
		{.cycle = 23, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 32, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 40, .bank = 0, .cmd = Data::MemCommand::RDA},
		{.cycle = 44, .bank = 1, .cmd = Data::MemCommand::RDA, .last = 1}
	},
	{
		{.cycle = 0, .bank = 0, .cmd = Data::MemCommand::ACT},
		{.cycle = 9, .bank = 1, .cmd = Data::MemCommand::ACT},
		{.cycle = 18, .bank = 2, .cmd = Data::MemCommand::ACT},
		{.cycle = 22, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 27, .bank = 3, .cmd = Data::MemCommand::ACT},
		{.cycle = 30, .bank = 0, .cmd = Data::MemCommand::RDA},
		{.cycle = 34, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 42, .bank = 1, .cmd = Data::MemCommand::RDA},
		{.cycle = 47, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 55, .bank = 2, .cmd = Data::MemCommand::RDA},
		{.cycle = 59, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 67, .bank = 3, .cmd = Data::MemCommand::RDA, .last = 1}
	},
	{
		{.cycle = 0, .bank = 0, .cmd = Data::MemCommand::ACT},
		{.cycle = 9, .bank = 1, .cmd = Data::MemCommand::ACT},
		{.cycle = 27, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 29, .bank = 2, .cmd = Data::MemCommand::ACT},
		{.cycle = 31, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 35, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 38, .bank = 3, .cmd = Data::MemCommand::ACT},
		{.cycle = 39, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 43, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 47, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 51, .bank = 0, .cmd = Data::MemCommand::RDA},
		{.cycle = 55, .bank = 1, .cmd = Data::MemCommand::RDA},
		{.cycle = 59, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 63, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 67, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 71, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 75, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 79, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 83, .bank = 2, .cmd = Data::MemCommand::RDA},
		{.cycle = 87, .bank = 3, .cmd = Data::MemCommand::RDA, .last = 1}
	},
	{
		{.cycle = 0, .bank = 0, .cmd = Data::MemCommand::ACT},
		{.cycle = 9, .bank = 1, .cmd = Data::MemCommand::ACT},
		{.cycle = 27, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 30, .bank = 2, .cmd = Data::MemCommand::ACT},
		{.cycle = 31, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 35, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 39, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 43, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 45, .bank = 3, .cmd = Data::MemCommand::ACT},
		{.cycle = 47, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 51, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 55, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 59, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 63, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 67, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 71, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 75, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 79, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 83, .bank = 0, .cmd = Data::MemCommand::RDA},
		{.cycle = 87, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 91, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 95, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 99, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 103, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 107, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 111, .bank = 1, .cmd = Data::MemCommand::RDA},
		{.cycle = 115, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 119, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 123, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 127, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 131, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 135, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 139, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 143, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 147, .bank = 2, .cmd = Data::MemCommand::RDA},
		{.cycle = 151, .bank = 3, .cmd = Data::MemCommand::RDA, .last = 1},
	},

	{
		{.cycle = 0, .bank = 0, .cmd = Data::MemCommand::ACT},
		{.cycle = 9, .bank = 1, .cmd = Data::MemCommand::ACT},
		{.cycle = 27, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 31, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 35, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 39, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 43, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 47, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 51, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 55, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 59, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 63, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 67, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 71, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 75, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 79, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 82, .bank = 2, .cmd = Data::MemCommand::ACT},
		{.cycle = 83, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 87, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 91, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 95, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 99, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 103, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 107, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 111, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 115, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 119, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 121, .bank = 3, .cmd = Data::MemCommand::ACT},
		{.cycle = 123, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 127, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 131, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 135, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 139, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 143, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 147, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 151, .bank = 1, .cmd = Data::MemCommand::RDA},
		{.cycle = 155, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 159, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 163, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 167, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 171, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 175, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 179, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 183, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 187, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 191, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 195, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 199, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 203, .bank = 0, .cmd = Data::MemCommand::RDA},
		{.cycle = 207, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 211, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 215, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 219, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 223, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 227, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 231, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 235, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 239, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 243, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 247, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 251, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 255, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 259, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 263, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 267, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 271, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 275, .bank = 2, .cmd = Data::MemCommand::RDA},
		{.cycle = 279, .bank = 3, .cmd = Data::MemCommand::RDA, .last = 1},
	},
	{
		{.cycle = 0, .bank = 0, .cmd = Data::MemCommand::ACT},
		{.cycle = 9, .bank = 1, .cmd = Data::MemCommand::ACT},
		{.cycle = 27, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 31, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 35, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 39, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 43, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 47, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 51, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 55, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 59, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 63, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 67, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 71, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 75, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 79, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 82, .bank = 2, .cmd = Data::MemCommand::ACT},
		{.cycle = 83, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 87, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 91, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 95, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 99, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 103, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 107, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 111, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 115, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 119, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 121, .bank = 3, .cmd = Data::MemCommand::ACT},
		{.cycle = 123, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 127, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 131, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 135, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 139, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 143, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 147, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 151, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 155, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 159, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 163, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 167, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 171, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 175, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 179, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 183, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 187, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 191, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 195, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 199, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 203, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 207, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 211, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 215, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 219, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 223, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 227, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 231, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 235, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 239, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 243, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 247, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 251, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 255, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 259, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 263, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 267, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 271, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 275, .bank = 0, .cmd = Data::MemCommand::RDA},
		{.cycle = 279, .bank = 1, .cmd = Data::MemCommand::RDA},
		{.cycle = 283, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 287, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 291, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 295, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 299, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 303, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 307, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 311, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 315, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 319, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 323, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 327, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 331, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 335, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 339, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 343, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 347, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 351, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 355, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 359, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 363, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 367, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 371, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 375, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 379, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 383, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 387, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 391, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 395, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 399, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 403, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 407, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 411, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 415, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 419, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 423, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 427, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 431, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 435, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 439, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 443, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 447, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 451, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 455, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 459, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 463, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 467, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 471, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 475, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 479, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 483, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 487, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 491, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 495, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 499, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 503, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 507, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 511, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 515, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 519, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 523, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 527, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 531, .bank = 2, .cmd = Data::MemCommand::RDA},
		{.cycle = 535, .bank = 3, .cmd = Data::MemCommand::RDA, .last = 1},
	},
	{
		{.cycle = 0, .bank = 0, .cmd = Data::MemCommand::ACT},
		{.cycle = 9, .bank = 1, .cmd = Data::MemCommand::ACT},
		{.cycle = 27, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 31, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 35, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 39, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 43, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 47, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 51, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 55, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 59, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 63, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 67, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 71, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 75, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 79, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 82, .bank = 2, .cmd = Data::MemCommand::ACT},
		{.cycle = 83, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 87, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 91, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 95, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 99, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 103, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 107, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 111, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 115, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 119, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 121, .bank = 3, .cmd = Data::MemCommand::ACT},
		{.cycle = 123, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 127, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 131, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 135, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 139, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 143, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 147, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 151, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 155, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 159, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 163, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 167, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 171, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 175, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 179, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 183, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 187, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 191, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 195, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 199, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 203, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 207, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 211, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 215, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 219, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 223, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 227, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 231, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 235, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 239, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 243, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 247, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 251, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 255, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 259, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 263, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 267, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 271, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 275, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 279, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 283, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 287, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 291, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 295, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 299, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 303, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 307, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 311, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 315, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 319, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 323, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 327, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 331, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 335, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 339, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 343, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 347, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 351, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 355, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 359, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 363, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 367, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 371, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 375, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 379, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 383, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 387, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 391, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 395, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 399, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 403, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 407, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 411, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 415, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 419, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 423, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 427, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 431, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 435, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 439, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 443, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 447, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 451, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 455, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 459, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 463, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 467, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 471, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 475, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 479, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 483, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 487, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 491, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 495, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 499, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 503, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 507, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 511, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 515, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 519, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 523, .bank = 0, .cmd = Data::MemCommand::RD},
		{.cycle = 527, .bank = 1, .cmd = Data::MemCommand::RD},
		{.cycle = 531, .bank = 0, .cmd = Data::MemCommand::RDA},
		{.cycle = 535, .bank = 1, .cmd = Data::MemCommand::RDA},
		{.cycle = 539, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 543, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 547, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 551, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 555, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 559, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 561, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 565, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 569, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 573, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 577, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 581, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 585, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 589, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 593, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 597, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 601, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 605, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 609, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 613, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 617, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 621, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 625, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 629, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 633, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 637, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 641, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 645, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 649, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 653, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 657, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 661, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 665, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 669, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 673, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 677, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 681, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 685, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 689, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 693, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 697, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 701, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 705, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 709, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 713, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 717, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 721, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 725, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 729, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 733, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 737, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 741, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 745, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 749, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 753, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 757, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 761, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 765, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 769, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 773, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 777, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 781, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 785, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 789, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 793, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 797, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 801, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 805, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 809, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 813, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 817, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 821, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 825, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 829, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 833, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 837, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 841, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 845, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 849, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 853, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 857, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 861, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 865, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 869, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 873, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 877, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 881, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 885, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 889, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 893, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 897, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 901, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 905, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 909, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 913, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 917, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 921, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 925, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 929, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 933, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 937, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 941, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 945, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 949, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 953, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 957, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 961, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 965, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 969, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 973, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 977, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 981, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 985, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 989, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 993, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 997, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 1001, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 1005, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 1009, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 1013, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 1017, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 1021, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 1025, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 1029, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 1033, .bank = 2, .cmd = Data::MemCommand::RD},
		{.cycle = 1037, .bank = 3, .cmd = Data::MemCommand::RD},
		{.cycle = 1041, .bank = 2, .cmd = Data::MemCommand::RDA},
		{.cycle = 1045, .bank = 3, .cmd = Data::MemCommand::RDA, .last = 1},
	},
};

void
pwr_issue_pattern(unsigned int bg, unsigned int bank)
{
	pret_ptrn *ptrn = ptrn_3200[bg];
	pret_ptrn_stats *ptrn_stats = &ptrn_3200_stats[bg];
	unsigned int i = 0;

	if (bg < 9) {
		do {
			pwr[bg]->doCommand(ptrn[i].cmd, ptrn[i].bank,
					ptrn[i].cycle + stats[bg].latency);
		} while (!ptrn[i++].last);
	}

	stats[bg].latency += ptrn_stats->latency;
	stats[bg].act += ptrn_stats->act;
	stats[bg].cas += ptrn_stats->cas;
	stats[bg].pre += ptrn_stats->pre;
}

void
print_stats(pret_ptrn_stats *stats, unsigned int bg)
{
	cout << "=== Stats ===" << endl;
	cout << "Longest issue delay  : " << stats->latency << endl;
	cout << "# Read/write ops     : " << stats->cas << endl;
	cout << "# Activate ops       : " << stats->act << endl;
	cout << "# Explicit PRE ops   : " << stats->pre << endl;

	pwr[bg]->calcEnergy();
	cout << "# Total energy (pJ)  : ";
	printf("%8.3lf\n", pwr[bg]->getEnergy().total_energy);
	cout << "# Average power (mW) : ";
	printf("%8.3lf\n", pwr[bg]->getPower().average_power);
}

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

uint64_t
bank(uint64_t addr)
{
	uint64_t b;
	
	b = (addr & 0x40ull) >> 6ull;
	b |= (addr & 0x1c000ull) >> 13ull;

	return b;
}

uint64_t
row(uint64_t addr)
{
	uint64_t r;

	r = (addr & 0x1fffc0000ull) >> 18ull;

	return r;
}

void
do_generate(unsigned int wordsize, unsigned int stride, unsigned int cycle,
		unsigned int burstgroups[9])
{
	unsigned int pos;
	uint64_t i;
	unsigned int j;
	uint64_t b, r;
	unsigned int have_skipped = 0;
	unsigned int skip;
	unsigned int skipcnt;
	unsigned int skiprst;
	uint64_t last_addr = UINT64_C(0xffffffffffffffff);
	uint64_t addr;
	uint64_t mask;
	
	uint64_t b_row[16];
	
	skipcnt = (stride - (wordsize + 15));
	skiprst = (skipcnt & 0xf);
	
	skipcnt &= ~0xf;
	
	memset(b_row, 0xff, 16*sizeof(uint64_t));
	memset(burstgroups, 0x0, 9*sizeof(unsigned int));

	for (i = 0; i < cycle; i++) {
		/* Modulo expensive, in HW use single subtraction instead */
		pos = i % stride;
		
		if (i % 16 == 0) {
			b = bank(i << UINT64_C(2));
			r = row(i << UINT64_C(2));
			printf_stride("%04" PRIx64" (%1" PRIu64", %5" PRIu64") ",
					i << UINT64_C(2), b, r);

			if (b_row[b] != r) {
				printf_stride("(A): ");
				b_row[b] = r;
			} else {
				printf_stride("   : ");
			}
			
			/* Determine groups */
			addr = i << UINT64_C(2);
			for (j = 0; j < 9; j++) {
				mask = ~((UINT64_C(1) << (j + 6)) - UINT64_C(1));
				if ((addr & mask) != (last_addr & mask)) {
					burstgroups[j]++;
					pwr_issue_pattern(j, 0);
				}
			}
			
			last_addr = addr;
		}
		
		if (pos < wordsize) {
			have_skipped = 0;
			printf_stride("X ");
		} else {
			printf_stride(". ");
		}
		
		if (i % 4 == 3)
			printf_stride(" ");
		
		if (i % 16 == 15) {
			printf_stride("\n");
			
			/* SKIP */
			if (!have_skipped) {
				skip = 0;
				if (pos >= wordsize-1) {
					skip = skipcnt;
					if (pos < wordsize+skiprst - 1)
						skip += 16;
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
}

void
help(char *bin_name)
{
	printf("Generate burst requests for stride pattern, and determine the\n"
	       "longest issue delay for a pattern-based DRAM controller using\n"
	       "a DDR4-3200AA two bank-group configuration\n"
	       "\n");
	printf("Usage: %s [options] <words> <period>\n", bin_name);
	printf("       %s -g [options] <words>\n", bin_name);
	printf("Options:\n");
	printf("\t-c [num]: Period count.\n");
	printf("\t-s      : Output Predator per-burstcount summary.\n");
	printf("\t-S      : Output graphical stride pattern.\n");
	printf("\t-g      : Output gnuplot-friendly longest issue delay for "
			"[1-<words>].\n");
}

void
parse_parameters(int argc, char* argv[], unsigned int *wordsize,
		unsigned int *stride, unsigned int *cycle, bool *aligned)
{
	int c;
	int minargs = 3;

	/* Two passes through options. First to determine whether we're doing
	 * a GNUPLOT run, in which case we can omit stride */
	while ( (c = getopt(argc-1, argv, "c:sSgla")) != -1) {
		switch (c) {
		case 'g':
			debug_output[DEBUG_OUTPUT_LID_GNUPLOT] = 1;
			minargs = 2;
			break;
		default:
			break;
		}
	}
	optind = 1;

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
	while ( (c = getopt(argc+1-minargs, argv, "c:sSgla")) != -1) {
		switch (c) {
		case 'c':
			*cycle = strtoul(optarg, NULL, 10) * (*stride);
			break;
		case 's':
			debug_output[DEBUG_OUTPUT_STATS] = 1;
			break;
		case 'S':
			debug_output[DEBUG_OUTPUT_STRIDE] = 1;
			break;
		case 'l':
			debug_output[DEBUG_OUTPUT_STATS_LATEX] = 1;
			break;
		case 'g':
			break;
		case 'a':
			*aligned = true;
			break;
		default:
			help(argv[0]);
			exit(1);
		}
	}

	if (!debug_output[DEBUG_OUTPUT_STATS] &&
			!debug_output[DEBUG_OUTPUT_STATS_LATEX] &&
			!debug_output[DEBUG_OUTPUT_STRIDE] &&
			!debug_output[DEBUG_OUTPUT_LID_GNUPLOT]) {
		printf("Error: no output format selected\n");
		help(argv[0]);
		exit(1);
	}

}

int main(int argc, char **argv)
{
	unsigned int wordsize;
	unsigned int stride;
	uint64_t i;
	unsigned int j;
	unsigned int cycle;
	pret_ptrn_stats *ptrn_stats;
	unsigned long min_latency = std::numeric_limits<unsigned long>::max();
	double min_energy = std::numeric_limits<double>::max();
	bool aligned = false;
	string memspecpath;

	unsigned int ptrn_no;
	unsigned int burstgroups[9] = {0,0,0,0,0,0,0,0,0};

	memspecpath = Data::MemSpecParser::getDefaultXMLPath();
	memspecpath.append("/memspecs/MICRON_8Gb_DDR4-3200_16bit_G.xml");
	memSpec = new Data::MemorySpecification(
		Data::MemSpecParser::getMemSpecFromXML(memspecpath));

	for (i = 0; i < 9; i++)
		pwr[i] = new libDRAMPower(*memSpec, 0);

	parse_parameters(argc, argv, &wordsize, &stride, &cycle, &aligned);

	if (debug_output[DEBUG_OUTPUT_LID_GNUPLOT]) {
		i = UINT64_C(1);
		cycle = wordsize;
	} else {
		i = wordsize;
	}

	for (; i <= wordsize; i++) {
		if (debug_output[DEBUG_OUTPUT_LID_GNUPLOT])
			do_generate(i, i, i, burstgroups);
		else
			do_generate(i, stride, cycle, burstgroups);

		if (debug_output[DEBUG_OUTPUT_LID_GNUPLOT]) {
			printf("%" PRIu64 " ", i);
			for (j = 0; j < 9; j++) {
				ptrn_no = burstgroups[j];
				ptrn_stats = &ptrn_3200_stats[j];

				if (!aligned && (i % (UINT64_C(16) << j)) != UINT64_C(1))
					ptrn_no += 1;

				printf("%lu ", ptrn_no * ptrn_stats->latency);
			}

			printf("\n");
		}
	}

	if (debug_output[DEBUG_OUTPUT_STATS]) {
		printf("\n");
		printf("Burst groups:\n");
		for (i = 0, j = 1; i < 9; i++, j <<= 1) {
			printf("%2u: %u patterns\n", j, burstgroups[i]);
			print_stats(&stats[i], i);
			cout << endl;
		}
	}

	if (debug_output[DEBUG_OUTPUT_STATS_LATEX]) {
		for (i = 0, j = 1; i < 7; i++, j <<= 1) {
			min_latency = std::min(min_latency,stats[i].latency);
		}

		for (i = 0, j = 1; i < 7; i++, j <<= 1) {
			if (stats[i].latency == min_latency) {
				cout << "\\textbf{" << stats[i].latency << "} & ";
				pwr[i]->calcEnergy();
				min_energy = std::min(min_energy,pwr[i]->getEnergy().total_energy);
			} else {
				cout << stats[i].latency << " & ";
			}
		}
		printf("%.1lf\\\\\n", min_energy/1000.);
	}
}
