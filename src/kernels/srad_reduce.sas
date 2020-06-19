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
	0 0x0000000 229916 1 f "data/srad/d_I_out.bin"
	1 0x1000000 229916 1 f "data/srad/d_sums2.bin"
	2 0x2000000      1 1 // d_mul

.sp_data
	0 0x0000 1024 1		// d_psum
	1 0x1000 1024 1		// d_psum2

.text
	// I already greatly simplified the original kernel in an effort of
	// deduplication. Even if performance stayed roughly equal, it makes the
	// code a lot more manageable and comprehensible.
	// Still it makes no sense to port the kernel 1-on-1. Firstly because
	// we have work-groups of 1024 threads, secondly because if we do, half
	// the threads will never perform any meaningful work.
	// As a side-effect, we won't be able to compare our intermediate results
	// with that of the SRAD pass. The final result, after all passes, should
	// roughly correspond though, as it's just two sums.
	sldg s6, 2								// s6: d_mul
	
	bufquery.dim_x s0, 1					// s0: d_Nr
	mov v0, vc.tid_x						// v0: tid_x, row
	sshr s2, s0, 1							// Divide by 2.
	imul v2, v0, s6							// Multiply tid_x by d_mul
	isub v3, v2, s2
	itest.ge p0, v3
	exit p0
	
	// The most efficient reduction with our arch is to load two adjacent
	// numbers, reduce, store a single into SP. Disable half the threads,
	// repeat until the WG reduced up to 2048 numbers down to 1. Then stick the
	// final results in a separate buffer 1/2048'th the size. 
	// However, for... reasons, the original kernel requires an in-place
	// reduction. Which means we need to perform indexed loads. Which is
	// expensive. As in: for the second pass we'll only achieve 6.25% DRAM
	// throughput - if we even bother describing custom stride patterns -
	// expensive.
	
	// First things first. I need the "last active thread LID". And for that 
	// thread, I should know whether I must load data or not.
	smov s1, sc.wg_off_x
	sisub s0, s0, 1
	sshl s1, s1, 1
	sidiv s0, s0, s6
	sisub s0, s0, s1
	simin s0, s0, 2047
	sand s3, s0, 0x1						// s3: Last thread must load/add
	sshr s0, s0, 1							// s0: last thread for this local thingmebob
	
	mov v5, vc.lid_x						// Initialise local scratchpad it
											
	simul s4, s1, s6
	imul v7, v5, s6
	iadd v7, v7, s4
	siadd s5, s0, 1							// s5: # periods
	
	smovssp sc.sd_words, 1
	#bound value 1
	smovssp sc.sd_period, s6
	#bound value 2048
	smovssp sc.sd_period_cnt, s5
	movvsp vc.mem_idx, v7
	
	ldgcidx 0, s4
	mov v1, vc.mem_data
	ldgcidx 1, s4
	mov v3, vc.mem_data
	
	simul s7, s5, s6
	iadd v31, v7, s7
	siadd s4, s4, s7
	
	smov s15, 1								// An elaborate way to reduce the 
	sisub s15, s15, s3						// period by 1 if the last thread
	sisub s15, s5, s15						// shouldn't read a second element
	
	#bound value 2048
	smovssp sc.sd_period_cnt s15
	
	movvsp vc.mem_idx, v31
	ldgcidx 0, s4
	mov v2, vc.mem_data
	ldgcidx 1, s4
	mov v4, vc.mem_data

	isub v7, v5, s0
			
loop_begin:
	// If I shifted out a 0 and I'm the last thread, don't perform the addition,
	// just copy v1, v3 to the new location.
	mov v6, 0
	iadd v6, v6, s3
	itest.ez p0, v6
	itest.ez p1, v7
	pbool.and p0, p0, p1
	cpush.if loop_done_add
	cmask p0
	add v1, v1, v2
	add v3, v3, v4
	cpop
	
loop_done_add:
	#bound branchcycle 1 10 1
	sicj.ez loop_end, s0

	stsplin v1, 0
	stsplin v3, 1
	
	// Do some break-ey stuff
	sand s3, s0, 0x1
	sshr s0, s0, 1
	isub v7, v5, s0
	itest.g p0, v7
	exit p0
	
	ldsplin.vec2 v1, 0
	ldsplin.vec2 v3, 1
	
	j loop_begin
	
loop_end:
	// We're just storing a single word to each buffer.
	smovssp sc.sd_period 1
	smovssp sc.sd_period_cnt 1
	simul s1, s1, s6
	mov v0, vc.zero
	iadd v0, v0, s1
	
	movvsp vc.mem_idx, v0
	movvsp vc.mem_data v1
	stgcidx 0, s1
	movvsp vc.mem_data v3
	stgcidx 1, s1
		
	exit