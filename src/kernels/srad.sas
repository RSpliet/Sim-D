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
	0 0x0000000 502   1 f "data/srad/d_iN.bin"
	1 0x0000800 502   1 f "data/srad/d_iS.bin"
	2 0x0001000   1 458 f "data/srad/d_jE.bin"
	3 0x0001800   1 458 f "data/srad/d_jW.bin"
	4 0x0100000 502 458 // d_dN
	5 0x0200000 502 458 // d_dS
	6 0x0300000 502 458 // d_dE
	7 0x0400000 502 458 // d_dW
	8 0x0500000 502 458 // d_c
	9 0x0600000 502 458 f "data/srad/d_I.bin"
   10 0x0002000   1   1 f "data/srad/srad_constants.bin"
	
.sp_data
	0 0x0 502 2

.text
	// The original kernel performs some reeeally strange arithmetic to map
	// 1D thread IDs to a 2D plane. We don't bother. Just use a 2D mapping and
	// calculate a 1D index in a buffer if need be. This kernel is definitely
	// DRAM bound!
	
	// Oh, by the way: this kernel assumes column-major data organisation.
	// Which is confusing.
	
	// Launch with a workgroup width of 512.
	
	// Early exit if my pixel is out of bounds
	bufquery.dim_x s0, 9					// s0: d_Nr
	bufquery.dim_y s1, 9					// s1: d_Nc
	mov v0, vc.tid_x						// v0: tid_x, row
	mov v1, vc.tid_y						// v1: tid_y, col
	isub v3, v1, s1
	isub v2, v0, s0
	itest.ge p0, v2
	itest.ge p1, v3
	pbool.or p0, p0, p1
	exit p0
	
	smov s3, sc.wg_off_y					// s3: col for thread 0
	
	sldg s2, 10, 1							// s2: d_q0sqr
	ldglin v2, 9							// v2: d_Jc
	
	// directional derivates. First get our d_iN and d_iS components.
	movvsp vc.mem_idx, v0
	ldgbidx 0
	mov v3, vc.mem_data						// v3: d_iN
	ldgbidx 1
	mov v4, vc.mem_data						// v4: d_iS
	
	// We can restrict these indexed loads of north+south to just two "columns"
	ldg2sptile 0, 9, 0, s3
	
	isub v13, v1, s3
	imad v12, v13, s0, v3
	movvsp vc.mem_idx, v12
	ldspbidx 0
	mov v3, vc.mem_data
	add.neg v3, v3, v2						// v3: d_dN_loc
	
	imad v12, v13, s0, v4
	movvsp vc.mem_idx, v12
	ldspbidx 0
	mov v4, vc.mem_data
	add.neg v4, v4, v2						// v4: d_dS_loc
	
	// Now d_jW and d_jE
	movvsp vc.mem_idx, v1
	ldgbidx 2
	mov v5, vc.mem_data						// v5: d_iE
	ldgbidx 3
	mov v6, vc.mem_data						// v6: d_iW
	
	// Practically these d_iE and d_iW values are just row plus/minus 1. This
	// means we're essentially just performing a very expensive way of loading
	// neighbouring pixels. The way the kernel is built up though, d_iE and d_iW
	// can be varied, so we may not assume such information. 
	// Hence we can't restrict east+west w/o information on the contents of
	// d_jW|E. Given the size of the buffer, iterative loads are probably
	// quickest.
	imad v12, v5, s0, v0
	movvsp vc.mem_idx, v12
	ldgidxit v5, 9
	add.neg v5, v5, v2						// v5: d_dE_loc
	
	imad v12, v6, s0, v0
	movvsp vc.mem_idx, v12
	ldgidxit v6, 9
	add.neg v6, v6, v2						// v6: d_dW_loc
	
	// d_G2: Normalised discrete gradient mag squared
	mul v7, v3, v3
	mad v7, v4, v4, v7
	mad v7, v5, v5, v7
	mad v7, v6, v6, v7
	mul v8, v2, v2
	rcp v8, v8
	mul v7, v7, v8							// v7: d_G2
	
	// d_L: normalised discrete laplacian
	rcp v9, v2
	add v8, v3, v4
	add v8, v8, v5
	add v8, v8, v6
	mul v8, v8, v9							// v8: d_L
	
	// ICOV
	mul v9, v8, v8
	mul v9, v9, -0.0625f
	mad v9, v7, 0.5, v9					// v9: d_num
	
	cvt.i2f v11, vc.one
	mad v10, v8, 0.5, v11					// v10: d_den
	
	mul v10, v10, v10
	rcp v10, v10
	mul v10, v9, v10						// v10: d_qsqr
	
	// diffusion coefficient
	add v11, v11, s2
	mul v11, v11, s2
	rcp v11, v11
	add.neg v9, v10, s2
	mul v9, v9, v11							// v9: d_den
	
	add v9, v9, 1.f
	rcp v9, v9
	min v9, v9, 1.f
	max v9, v9, 0.f							// v9: d_c_loc
			
	stglin v3, 4
	stglin v4, 5
	stglin v5, 6
	stglin v6, 7
	stglin v9, 8
	
	exit