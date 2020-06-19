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
// id addr      x   y    Type Input file                         Thread-stride
	0 0x0       111 7104 d    "data/cnn_maxpool/in.csv"
	1 0x2000000 1   1    d    "data/cnn_maxpool/constants.csv"
	2 0x1000000 55  3520
	
.sp_data
	0 0x0		65   65		// Because stride is 2, dimensions are 2*wg_width+1
	
.text
	// Disable threads outside bounds.
	sldg s7, 1						// Pooling factor (3)
	scvt.f2i s7, s7
	smov s0, sc.dim_x
	smov s8, sc.wg_off_y
	sshr s2, s8, 6					// Channel. Every channel is mapped to a
									// "row" of 64 threads, so that an entire
									// WG works on a single channel.
	bufquery.dim_x s4, 2			// Threads in y dimension (per-channel)
	mov v0, vc.tid_x
	mov v1, vc.tid_y
	and v1, v1, 63
	isub v0, v0, s0
	isub v1, v1, s4
	itest.ge p0, v0
	//printpr p0
	itest.ge p1, v1
	//printpr p1
	pbool.or p0, p0, p1
	exit p0
		
	// Channel (z-dimension) offset boilerplate
	bufquery.dim_x s4, 0 
	simul s3, s2, s4				// Y-offset of channel

	smov s9, sc.wg_off_x
	sand s8, s8, 63
	simul s9, s9, 2					// Start of tile, X-dim
	simul s8, s8, 2					
	siadd s8, s8, s3				// Start of tile, Y-dim
	ldg2sptile 0, 0, s9, s8
	
	mov v1, -FLT_MAX				// maxSoFar
	smov s1, 0						// dy
	
	mov v3, vc.lid_y
	imul v3, v3, 2
	
loop_y:
	mov v4, vc.lid_x
	smov s0, 0						// dx
	imul v4, v4, 2
	imad v5, v3, 65, v4 
loop_x:
	movvsp vc.mem_idx, v5
	
	// TODO: Using custom stride patterns we should be able to skip every other
	// line, double net throughput.
	ldspbidx 0
	mov v2, vc.mem_data
	max v1, v1, v2
	
	siadd s0, s0, 1
	iadd v5, v5, 1
	sisub s3, s0, s7
	#bound branchcycle 2 1 0
	sicj.l loop_x, s3

	siadd s1, s1, 1
	iadd v3, v3, 1
	sisub s3, s1, s7
	#bound branchcycle 2 1 0
	sicj.l loop_y, s3
		
	// Calculate y offset
	simul s2, s2, -9
		
	stglin v1, 2, 0, s2
	exit