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
	0 0x0       640 480 d "data/kfusion/halfSampleRobustImage_in.csv"
	1 0x180000 1920 480 //  -- float3 stored 16B aligned
	2 0x680000    4   4 d "data/kfusion/depth2vertex_invK.csv"

.sp_data
	// We need to write back packed vec3 data. Which is a tad painful. Use a SP
	// To prepare the data structure, write back with a single DRAM stg2sptile
	0 0x0 192 16

.text
	// Disable unused threads
	mov v0, vc.tid_x
	mov v1, vc.tid_y
	bufquery.dim_x s0, 0
	bufquery.dim_y s1, 0
	isub v2, v0, s0
	isub v3, v1, s1
	itest.ge p0, v2
	itest.ge p1, v3
	pbool.or p0, p0, p1
	exit p0
	
	ldglin v2, 0
	cvt.i2f v3, vc.zero
	cvt.i2f v4, vc.zero
	cvt.i2f v5, vc.zero
	cvt.i2f v6, vc.zero
	sldg s2, 2, 11				// We're only using the xyz components
	cpush.if out
	test.le p0, v2
	cmask p0
	
	// Let's go MAD
	cvt.i2f v0, vc.tid_x
	cvt.i2f v1, vc.tid_y
	cvt.i2f v7, vc.one
	
	mul v3, v0, s2
	mul v4, v0, s6
	mul v5, v0, s10
	mad v3, v1, s3, v3
	mad v4, v1, s7, v4
	mad v5, v1, s11, v5
	mad v3, v7, s4, v3
	mad v4, v7, s8, v4
	mad v5, v7, s12, v5
	mul v3, v3, v2
	mul v4, v4, v2
	mul v5, v5, v2
	cpop
	
out:
	mov v0, vc.lid_x
	mov v1, vc.lid_y
	imul v0, v0, 3
	imad v0, v1, 192, v0
	
	movvsp vc.mem_idx, v0
	movvsp vc.mem_data v3
	stspbidx 0
	
	iadd v0, v0, 1
	movvsp vc.mem_idx, v0
	movvsp vc.mem_data v4
	stspbidx 0
	
	iadd v0, v0, 1
	movvsp vc.mem_idx, v0
	movvsp vc.mem_data v5
	stspbidx 0
	
	smov s0, sc.wg_off_x
	smov s1, sc.wg_off_y
	simul s0, s0, 3
	
	stg2sptile 0, 1, s0, s1
	
	exit