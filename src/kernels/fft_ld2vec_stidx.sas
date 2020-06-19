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
	0 0x000000 512 1024 f "data/fft/in.bin"
	1 0x200000 512 1024
	2 0x400000   1    1 f "data/fft/constants.bin"

.text
	// Finally a benchmark where we can shine with Vec2 loads!
	// R is hard-coded to 2. No need for thread bounds checks.
	sldg s0, 2, 1						// s0: Ns
	bufquery.dim_x s1, 1				// s1: N
	smov s4, sc.wg_off_y				// Start of data0 windows, y offset
	
	// Load v[0] and v[1]
	ldglin.vec2 v0, 0					// v[0].xy
	ldglin.vec2 v2, 0, 128				// v[1].xy
	
	// angle.
	// Ns is always a power of two. So we can do modulo by ANDing with Ns-1
	mov v10, vc.tid_x					// v10: tid_x
	sisub s3, s0, 1						// s3: Ns - 1
	sshl s5, s0, 1
	and v11, v10, s3					// v11: tid_x & (Ns - 1)
	scvt.i2f s5, s5
	cvt.i2f v4, v11
	mov v31, vc.zero
	add v31, v31, s5
	rcp v31, v31
	mul v4, v4, -M_2PI_F				// const: -2*M_PI_F
	mul v4, v4, v31						// v4: angle
	
	// cmpMul. v[i].x*v6-v[i].y*v7, v[i].x*v7+v[i].y*v6, 
	mov v5, vc.zero
	cos v6, v5
	sin v7, v5
	mul.neg v8, v1, v7
	mul v9, v1, v6
	mad v1, v0, v7, v9
	mad v0, v0, v6, v8

	cos v6, v4
	sin v7, v4
	mul.neg v8, v3, v7
	mul v9, v3, v6
	mad v3, v2, v7, v9
	mad v2, v2, v6, v8
	
	// GPU_FFT2. New results in v6-v9
	add v6, v0, v2
	add v7, v1, v3
	add.neg v8, v0, v2
	add.neg v9, v1, v3
	
	// GPU_expand: (tid_x/Ns)*Ns*R + (tid_x%Ns). Ns power-of-two, R hardcoded 2.
	// (tid_x & ~(Ns-1) * R) | (tid_x & (Ns-1))
	snot s6, s3
	and v10, v10, s6
	shl v10, v10, 1
	or v10, v10, v11					// v10: idxD
	mov v0, vc.tid_y
	imul v10, v10, 2
	imad v10, v0, s1, v10
	
	smovssp sc.sd_words, 4096
	smovssp sc.sd_period, 4096
	smovssp sc.sd_period_cnt 1
	
	movvsp vc.mem_idx, v10
	movvsp vc.mem_data, v6
	stgcidx 1, 0, s4
	
	iadd v11, v10, 1
	movvsp vc.mem_idx, v11
	movvsp vc.mem_data, v7
	stgcidx 1, 0, s4
	
	iadd v10, v10, s0
	iadd v10, v10, s0
	
	movvsp vc.mem_idx, v10
	movvsp vc.mem_data, v8
	stgcidx 1, 0, s4
	
	iadd v11, v10, 1
	movvsp vc.mem_idx, v11
	movvsp vc.mem_data, v9
	stgcidx 1, 0, s4
	
	exit