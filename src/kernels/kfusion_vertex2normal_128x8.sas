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
	0 0x0      1920 480 f "data/kfusion/depth2vertex_out.bin"
	1 0x400000 1920 480

.sp_data
	0 0x0 390 10
	1 0x4000 384 8

.text
	// Disable unused threads
	mov v0, vc.tid_x
	mov v1, vc.tid_y
	bufquery.dim_x s0, 0
	bufquery.dim_y s1, 0
	sidiv s2, s0, 3
	isub v2, v0, s2
	isub v3, v1, s1
	itest.ge p0, v2
	itest.ge p1, v3
	pbool.or p0, p0, p1
	exit p0
	
	// Load a tile of data.
	smov s2, sc.wg_off_x
	smov s3, sc.wg_off_y
	sisub s4, s3, 1
	simul s6, s2, 3
	sisub s2, s6, 3
	ldg2sptile 0, 0, s2, s4
	
	// Find LID clamp values for the x element
	sineg s5, s4
	sineg s4, s2
	sisub s6, s0, s6
	sisub s7, s1, s3
	
	simax s4, s4, 0				// clamp for -x within sp tile
	simax s5, s5, 0				// clamp for -y within sp tile
	simin s6, s6, 388			// clamp for +x within sp tile. Note bufdim-3
								// to point to last vec3 element at offset
								// (99, 100, 101)
	simin s7, s7, 9			// clamp for +y within sp tile.
		
	mov v2, vc.lid_x			// lid_x. X-offset within tile.
	mov v3, vc.lid_y			// lid y. Y-offset within tile.
	imul v2, v2, 3
	
	////////////////////////////////////////////////////////////////////
	// x, y-1
	iadd v4, v2, 3				// x must exist by virtue of this thread
								// being enabled.
	imax v5, v3, s5	
	imad v6, v5, 390, v4
	
	movvsp vc.mem_idx, v6
	ldspbidx 0
	mov v10, vc.mem_data
	
	iadd v6, v6, 1
	movvsp vc.mem_idx, v6
	ldspbidx 0
	mov v11, vc.mem_data
	
	iadd v6, v6, 1
	movvsp vc.mem_idx, v6
	ldspbidx 0
	mov v12, vc.mem_data
	
	// x, y+1
	iadd v5, v3, 2
	imin v5, v5, s7
	imad v6, v5, 390, v4
	
	movvsp vc.mem_idx, v6
	ldspbidx 0
	mov v13, vc.mem_data
	
	iadd v6, v6, 1
	movvsp vc.mem_idx, v6
	ldspbidx 0
	mov v14, vc.mem_data
	
	iadd v6, v6, 1
	movvsp vc.mem_idx, v6
	ldspbidx 0
	mov v15, vc.mem_data

	// Calculate dyv.
	add.neg v10, v13, v10
	add.neg v11, v14, v11
	add.neg v12, v15, v12
	
	////////////////////////////////////////////////////////////////////
	// x-1, y
	iadd v5, v3, 1				// y is within bounds.
	imax v6, v2, s4
	imad v6, v5, 390, v6
	
	movvsp vc.mem_idx, v6
	ldspbidx 0
	mov v13, vc.mem_data
	
	iadd v6, v6, 1
	movvsp vc.mem_idx, v6
	ldspbidx 0
	mov v14, vc.mem_data
	
	iadd v6, v6, 1
	movvsp vc.mem_idx, v6
	ldspbidx 0
	mov v15, vc.mem_data
	
	// x+1, y
	iadd v4, v2, 6
	imin v6, v4, s6
	imad v6, v5, 390, v6
	
	movvsp vc.mem_idx, v6
	ldspbidx 0
	mov v7, vc.mem_data
	
	iadd v6, v6, 1
	movvsp vc.mem_idx, v6
	ldspbidx 0
	mov v8, vc.mem_data
	
	iadd v6, v6, 1
	movvsp vc.mem_idx, v6
	ldspbidx 0
	mov v9, vc.mem_data
	
	// calculate dxv
	add.neg v7, v7, v13
	add.neg v8, v8, v14
	add.neg v9, v9, v15
	
	// Cross product of (v10, v11, v12) X (v7, v8, v9)
	// Stick the negative in v13-v15, then mad the final results into them
	mul.neg v13, v12, v8
	mul.neg v14, v10, v9
	mul.neg v15, v11, v7
	
	mad v13, v11, v9, v13
	mad v14, v12, v7, v14
	mad v15, v10, v8, v15
	
	// v7-12 should now be free. Normalise the results.
	mul v7, v13, v13
	mad v7, v14, v14, v7
	mad v7, v15, v15, v7
	rsqrt v7, v7
	mul v13, v13, v7
	mul v14, v14, v7
	mul v15, v15, v7
	
	// Store
	imad v4, v3, 384, v2
	movvsp vc.mem_idx, v4
	movvsp vc.mem_data v13
	stspbidx 1
	
	iadd v4, v4, 1
	movvsp vc.mem_idx, v4
	movvsp vc.mem_data v14
	stspbidx 1
	
	iadd v4, v4, 1
	movvsp vc.mem_idx, v4
	movvsp vc.mem_data v15
	stspbidx 1
	
	smov s2, sc.wg_off_x
	smov s3, sc.wg_off_y
	simul s2, s2, 3
	
	stg2sptile 1, 1, s2, s3
	
	exit