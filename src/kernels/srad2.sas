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
	0 0x000000 502   1 f "data/srad/d_iS.bin"
	1 0x000800   1 458 f "data/srad/d_jE.bin"
	2 0x001000 502 458 f "data/srad/d_dN.bin"
	3 0x101000 502 458 f "data/srad/d_dS.bin"
	4 0x201000 502 458 f "data/srad/d_dE.bin"
	5 0x301000 502 458 f "data/srad/d_dW.bin"
	6 0x401000 502 458 f "data/srad/d_c.bin"
	7 0x501000 502 458 f "data/srad/d_I.bin"
	8 0x601000   1   1 f "data/srad/srad2_constants.bin"

.text
	// Early exit if my pixel is out of bounds
	bufquery.dim_x s0, 7					// s0: d_Nr
	bufquery.dim_y s1, 7					// s1: d_Nc
	mov v0, vc.tid_x						// v0: tid_x, row
	mov v1, vc.tid_y						// v1: tid_y, col
	isub v3, v1, s1
	isub v2, v0, s0
	itest.ge p0, v2
	itest.ge p1, v3
	pbool.or p0, p0, p1
	exit p0
	
	ldglin v2, 6							// v2: d_cN, d_cW
	
	movvsp vc.mem_idx, v0
	ldgbidx 0
	mov v3, vc.mem_data						// v3: d_iS[row]
	
	movvsp vc.mem_idx, v1
	ldgbidx 1
	mov v4, vc.mem_data						// v4: d_jE[col]
	
	smov s3, sc.wg_off_y					// s3: col of thread 0
	smovssp sc.sd_words, 1004
	smovssp sc.sd_period, 1004
	smovssp sc.sd_period_cnt, 1
	imad v3, v1, s0, v3
	movvsp vc.mem_idx, v3
	ldgcidx 6, 0, s3
	mov v3, vc.mem_data						// v3: d_cS
	
	imad v4, v4, s0, v1
	movvsp vc.mem_idx, v4
	ldgidxit v4, 6							// v4: d_cE
	
	ldglin v5, 2							// v5: d_dN
	mul v5, v5, v2
	
	ldglin v6, 3							// v6: d_dS
	mad v5, v6, v3, v5
	
	ldglin v6, 4							// v6: d_dE
	mad v5, v6, v2, v5 
	
	ldglin v6, 5							// v6: d_dW
	mad v5, v6, v4, v5						// v5: d_D
	
	mul v5, v5, .25
	
	ldglin v6, 7							// v6: d_I
	sldg s2, 8								// s2: d_lambda
	
	mad v5, v5, s2, v6						// v5: new value for d_I
	stglin v5, 7
	
	exit