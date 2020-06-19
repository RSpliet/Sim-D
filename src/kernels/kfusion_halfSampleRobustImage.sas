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
	0 0x0 640 480 d "data/kfusion/halfSampleRobustImage_in.csv"
	1 0x280000 4 1 d "data/kfusion/halfSampleRobustImage_constants.txt"
	2 0x140000 320 240

.sp_data
	0 0x0 64 64

.text
	// This kernel doesn't have to be more complex than it is. We're doing half-
	// sample, but the benchmark always invokes it with r=1. That means there's
	// no edge cases to worry about and my tile size is just 2x * 2y.
	sldg s0, 1, 2
	scvt.f2i s1, s1
	
	// Disable unused threads
	mov v0, vc.tid_x
	mov v1, vc.tid_y
	bufquery.dim_x s2, 2
	bufquery.dim_y s3, 2
	isub v0, v0, s2
	isub v1, v1, s3
	itest.ge p0, v0
	itest.ge p1, v1
	pbool.or p0, p0, p1
	exit p0
	
	smov s4, sc.wg_off_x
	smov s5, sc.wg_off_y
	cvt.i2f v0, vc.zero				// v0: sum
	cvt.i2f v1, vc.zero				// v1: t
	simul s4, s4, 2
	simul s5, s5, 2
	mov v2, vc.lid_x				// v2: SP offset X
	mov v3, vc.lid_y				// v3: SP offset y
	imul v2, v2, 2
	imul v3, v3, 2
	ldg2sptile 0, 0, s4, s5
	
	// First pick off centre pixel
	imad v2, v3, 64, v2				// v2: SP index
	movvsp vc.mem_idx, v2
	ldspbidx 0
	mov v6, vc.mem_data				// v6: centre
	
	// Consensual loop
	// s4: i
	// s5: j
	smov s4, 0
	simul s1, s1, 2
	
loop_i:
	smov s5 0
	
loop_j:
	movvsp vc.mem_idx, v2
	ldspbidx 0
	mov v4, vc.mem_data

	add.neg v5, v4, v6
	abs v5, v5
	add.neg v5, v5, s0
	cpush.if end_cond
	test.g p0, v5
	cmask p0
		
	add v0, v0, 1.f
	add v1, v1, v4
	cpop
	
end_cond:
	siadd s5, s5, 1
	iadd v2, v2, 1 
	sisub s6, s5, s1
	#bound branchcycle 1 1 0
	sicj.l loop_j, s6
	
	siadd s4, s4, 1
	iadd v2, v2, 62
	sisub s6, s4, s1
	#bound branchcycle 1 1 0
	sicj.l loop_i, s6
	
	rcp v0, v0
	mul v0, v1, v0
	
	stglin v0, 2

	exit
