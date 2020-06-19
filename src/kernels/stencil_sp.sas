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

// The big challenge in this program is that the data set is 3D, but the
// accelerator only 2D. We therefore have to assume the y dimension is really
// y*z. That makes y-offset calculation for linear loads and stores a little
// finicky.
// Pretty sure this kernel is I/O bound.

.data
// id addr     x   y    Type Input file           Thread-stride (1)
	0 0x0      128 4096 f "data/stencil/A0.bin"
	1 0x400000 3   1    d "data/stencil/constants.txt"
	2 0x200000 128 4096 f "data/stencil/A0.bin" // For comparison w/ Parboil.

.sp_data
	0 0x0      128 10

.text
	// Load some constants
	sldg s2, 1, 3
	scvt.f2i s2, s2
	
	// Disable threads outside bounds.
	// Program is launched in the y dimension in multiples of 128, such that
	// a workgroup only hits a single channel and to simplify calculating
	// the y-offset below. Disable lines 127 and 128.
	bufquery.dim_x s1, 0
	mov v0, vc.tid_y
	mov v1, vc.tid_x
	sisub s1, s1, 1
	sisub s5, s2, 2
	smov s0, sc.dim_x
	smov s6, sc.wg_off_y
	and v0, v0, s1	// Buffer dimension assumed to be power-of-two.
	siadd s6, s6, s2
	sisub s7, s2, 1
	sisub s1, s1, 1
	isub v1, v1, s0
	isub v0, v0, s5
	itest.ge p1, v1
	itest.ge p0, v0
	pbool.or p0, p0, p1
	exit p0

	// Channel z+0, offset 1,1
	ldglin v0, 0, 1, 1

	// Channel z+1, offset 1,0
	siadd s5, s2, 1	// Channel c+1, line 1
	ldg2sptile 0, 0, 0, s6
	mov v3, vc.lid_y
	mov v1, vc.lid_x
	imad v3, v3, s2, v1
	iadd v3, v3, 1
	movvsp vc.mem_idx, v3
	ldspbidx 0
	mov v1, vc.mem_data
	add v0, v1, v0

	// Channel z+1, offset 0,1
	iadd v3, v3, s7
	movvsp vc.mem_idx, v3
	ldspbidx 0
	mov v1, vc.mem_data
	add v0, v1, v0

	// Channel z+1, offset 1,1
	iadd v3, v3, 1
	movvsp vc.mem_idx, v3
	ldspbidx 0
	mov v2, vc.mem_data

	// Channel z+1, offset 2,1
	iadd v3, v3, 1
	movvsp vc.mem_idx, v3
	ldspbidx 0
	mov v1, vc.mem_data
	add v0, v1, v0

	// Channel z+1, offset 1,2
	iadd v3, v3, s7
	movvsp vc.mem_idx, v3
	ldspbidx 0
	mov v1, vc.mem_data
	add v0, v1, v0

	// Channel z+2, offset 1,1
	siadd s5, s5, s2 // Channel c+1, line 1
	ldglin v1, 0, 1, s5
	add v0, v1, v0
	mul v0, v0, s4

	// Prepare for stlin
	siadd s2, s2, 1

	// Channel z+1, offset 1,1
	mad.neg v0, v2, s3, v0

	// Store to correct offset in the buffer.
	stglin v0,2,1,s2

	exit
