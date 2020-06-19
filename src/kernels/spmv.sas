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
	0 0x000000 150144 1 f "data/spmv/data.bin"
	1 0x100000 150144 1 f "data/spmv/indices.bin"
	2 0x200000 11948 1 f "data/spmv/perm.bin"
	3 0x210000 11948 1 f "data/spmv/x_vector.bin"
	4 0x220000 50 1 f "data/spmv/jds_ptr_int.bin"
	5 0x2200d0 374 1 f "data/spmv/sh_zcnt_int.bin"
	6 0x221800 11948 1 // dst_vector
	
.sp_data
	0 0x0 50 1 // Local copy of jds_ptr_int

.text
	// This is one of those kernels that assumes a warp is 32 lanes wide. Poor
	// assumption: it's not true on AMD, it's not for us and I'd be surprised
	// if it's true on FPGAs. We'll keep our work-group of size 1024, and keep
	// the loop invariant static for 0..48. The onus of bounding the
	// loop iteration count is on the dev, by analysing the upper bound on
	// sh_zcnt_int. I've done so, and concluded it's 49 for this dataset.

	// Bail if thread out-of-bounds
	mov v2, vc.tid_x
	bufquery.dim_x s0, 2
	isub v0, v2, s0
	test.ge p0, v0
	exit p0
	
	smov s0, sc.wg_off_x
	shr v3, v2, 5
	sshr s0, s0, 5
	
	// Launch a custom indexed load to minimise the number of streamed words.
	smovssp sc.sd_words, 32
	smovssp sc.sd_period, 32
	smovssp sc.sd_period_cnt, 1
	movvsp vc.mem_idx, v3
	ldgcidx 5, s0				// v3: bound
	mov v3, vc.mem_data
		
	cvt.i2f v0, vc.zero				// v0: sum
	smov s1, 0						// s1: loop counter
	
	// Preload jds_ptr_int
	ldg2sptile 0, 4
	
	smovssp sc.sd_words 1
	
loop_begin:
	sldsp s2, 0, s1 				// s2: jds_ptr_int[k]
	
	// j is implicit
	ldglin v4, 1, s2				// v4: in
	ldglin v5, 0, s2				// v5: d
	
	movvsp vc.mem_idx, v4
	ldgbidx 3
	mov v6, vc.mem_data				// v6: t
	
	isub v31, v3, s1
	itest.le p0, v31				// p0 contains all the threads that are out
	// This mad needs to be conditional
	cpush.if loop_inv
	cmask p0
	mad v0, v5, v6, v0
	cpop
	
loop_inv:
	// Loop invariant
	siadd s1, s1, 1
	sisub s8, s1, 49				// 49 is the maximum of all values in the
									// benchmark. We do waste lots of cycles
									// on average, but not so much in the worst
									// case.
	#bound branchcycle 48 1 0
	sicj.l loop_begin, s8 
	
	// Load d_perm[ix]
	ldglin v4, 2
	
	// Store sum. Estimated 2897 cycles using CAM method, 8274 cycs iterative.
	movvsp vc.mem_idx, v4
	movvsp vc.mem_data v0
	stgbidx 6
	
	exit