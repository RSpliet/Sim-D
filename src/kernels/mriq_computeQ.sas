// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copyright (C) 2020 Roy Spliet, University of Cambridge
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// this program. If not, see <https://www.gnu.org/licenses/>.

.data
	0 0x580000 2 1 d "data/mri-q/computeQ_constants.txt"
	1 0x000000 262144 1 d "data/mri-q/x.csv"
	2 0x100000 262144 1 d "data/mri-q/y.csv"
	3 0x200000 262144 1 d "data/mri-q/z.csv"
	4 0x300000 262144 1	f "/dev/null"			// Qr, Zero
	5 0x400000 262144 1	f "/dev/null"			// Qi, Zero
	6 0x500000 8192 1 d "data/mri-q/kvalues.csv"

.sp_data
	// Cache kvalues in a block of 4KiB (256 entries), gives ~80% DRAM throughput
	0 0x0 1024 1

.text
	// This kernel is an example of one where data load and compute load must
	// be separated on the coarsest grain imaginable. First all loads, then
	// all compute, followed by all-store. This means the "overlapping compute
	// with data load/store" opportunities are very very slim, leading to
	// degradation of performance.
	ldglin v0, 1
	ldglin v1, 2
	ldglin v2, 3
	ldglin v3, 4
	smov s4, 0
	bufquery.dim_x s5, 6
	ldglin v4, 5
	smovssp sc.sd_words, 4		// The sldsp will load 4 words from the
								// scratchpad. Other loads are unaffected.
	
loop_compute:
	sand s3, s4, 1023
	#bound branchcycle 255 1 255
	sicj.nz skip_kindex_load, s3
	
	smov s6, 0					// In-scratchpad data iterator/pointer
								// TODO: This smov ends up blocking sldsp even
								// in the common case where the load is skipped.
								// Add flushing support to Scoreboard to improve
								// throughput - could end up saving up to
								// ~0.5M cycles on the total kernel.
	ldg2sptile 0, 6, s4
skip_kindex_load:
	sldsp s7, 0, s6 			// s7: ck.x, s8: ck.y, s9: ck.z, s10: ck.phiMag
	
	// A lot of compute
	mul v5, v0, s7
	siadd s4, s4, 4				// 4* loop counter, offset into kvalues to read
								// to SP.
	mad v5, v1, s8, v5
	sisub s3, s4, s5
	mad v5, v2, s9, v5
	siadd s6, s6, 4				// offset in scratchpad
	mul v5, v5, M_2PI_F
	cos v6, v5
	sin v7, v5
	mul v6, v6, s10
	mul v7, v7, s10
	add v3, v3, v6
	add v4, v4, v7

	#bound branchcycle 2047 1 0
	sicj.l loop_compute, s3
		
	stglin v3, 4
	stglin v4, 5
	exit