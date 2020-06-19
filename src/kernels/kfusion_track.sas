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
	0 0x0000000 1920 480 f "data/kfusion/depth2vertex_out.bin" // inVertex
	1 0x0400000 1920 480 f "data/kfusion/vertex2normal_out.bin" // inNormal
	2 0x0800000 1920 480 d "data/kfusion/track_refVertex.csv"
	3 0x0c00000 1920 480 d "data/kfusion/track_refNormal.csv"
	4 0x1000000 4    6   d "data/kfusion/track_transformMats.csv"
	5 0x1000060 2    1   d "data/kfusion/track_constants.csv" // constants
	6 0x1000100 5120 480 // output
	
	// Comparison of results is tricky, because the original benchmark leaves
	// parts of the output buffer uninitialised if the status code is set to
	// anything other than 1. Implement a custom comparison?

.sp_data
	0 0x0 256 32			// Result. Array-of-struct, 8 words per element:
							//     int result
							//     float error
							//     float J[6]
							// This uses up 32KiB of scratchpad, quite
							// significant considering NVIDIAs SP can only hold
							// up to 48KiB per work-group (64KiB on Turing).
	1 0x8000 4 6			// Ttrack, view

.text
	// The biggest challenge of this kernel is that even if we decide to bail
	// early, we still need to execute all the memory requests. If we don't, we
	// risk violating our condition that all threads must execute all memory
	// accesses. This is vital to limit the number of potential schedules.
	//
	// This is the only exception. If all threads exit here, they shouldn't have
	// been launched in the first place.
	// Early exit if my pixel is out of bounds
	bufquery.dim_x s0, 6
	bufquery.dim_y s1, 6
	mov v0, vc.tid_x
	mov v1, vc.tid_y
	sshr s0, s0, 3					// Divide by 8
	isub v3, v1, s1
	isub v2, v0, s0
	itest.ge p0, v2
	itest.ge p1, v3
	pbool.or p0, p0, p1
	exit p0

	mov v28, vc.zero
	mov v29, vc.zero
	mov v30, vc.zero
	mov v31, vc.zero

	// From here on we dedicate p0 as the predicate that contains what would be
	// the break mask, not committing results. p1 will be the inverse.
	// We only break after the last memory request has been issued.

	ldg2sptile 1, 4						// Preload transformation matrices.

	// We'll use the break mask to early-exit when conditions don't hold.
	cpush.brk store

	// Create a custom stride pattern
	bufquery.dim_x s2, 0
	smovssp sc.sd_words, 96
	#bound value 1920
	smovssp sc.sd_period, s2
	smovssp sc.sd_period_cnt, 32

	// Calculate indexes to load from
	smov s1, sc.wg_off_x
	smov s3, sc.wg_off_y
	imul v15, v1, s2
	imad v15, v0, 3, v15
	simul s1, s1, 3

	// Load float3s from inNormal
	movvsp vc.mem_idx, v15
	ldgcidx 1, s1, s3
	mov v3, vc.mem_data					// v3: normalPixel.x

	// If equal to -2., pixel is INVALID
	// p0 contains the "we should break out, not commit" predicate.
	// p1 contains the "we should continue" condition.
	add v4, v3, 2.
	test.nz p1, v4						// p1 Valid
	test.ez p0, v4						// p0 Invalid
	cpush.if endInvNormal
	cmask p1
	mov v0, -1							// cmov could help here.
	cpop

endInvNormal:
	iadd v15, v15, 1
	movvsp vc.mem_idx, v15
	ldgcidx 1, s1, s3
	mov v4, vc.mem_data					// v4: normalPixel.y

	iadd v15, v15, 1
	movvsp vc.mem_idx, v15
	ldgcidx 1, s1, s3
	mov v5, vc.mem_data					// v5: normalPixel.z

	// Now load our inVertex element
	isub v15, v15, 2
	movvsp vc.mem_idx, v15
	ldgcidx 0, s1, s3
	mov v6, vc.mem_data					// v6: vertexPixel.x

	iadd v15, v15, 1
	movvsp vc.mem_idx, v15
	ldgcidx 0, s1, s3
	mov v7, vc.mem_data					// v7: vertexPixel.y

	iadd v15, v15, 1
	movvsp vc.mem_idx, v15
	ldgcidx 0, s1, s3
	mov v8, vc.mem_data					// v8: vertexPixel.z

	// Project. projectedVertex will go into v9-v11
	smovssp sc.sd_words, 12
	sldsp s2, 1

	mul v9, v6, s2
	mul v10, v6, s6
	mul v11, v6, s10

	mad v9, v7, s3, v9
	mad v10, v7, s7, v10
	mad v11, v7, s11, v11

	mad v9, v8, s4, v9
	mad v10, v8, s8, v10
	mad v11, v8, s12, v11

	add v9, v9, s5						// v9: ProjectedVertex.x
	add v10, v10, s9					// v10: ProjectedVertex.y
	add v11, v11, s13					// v11: ProjectedVertex.z

	// projectedPos in v12-14
	sldsp s2, 1, 0, 3

	mul v12, v9, s2
	mul v13, v9, s6
	mul v14, v9, s10

	mad v12, v10, s3, v12
	mad v13, v10, s7, v13
	mad v14, v10, s11, v14

	mad v12, v11, s4, v12
	mad v13, v11, s8, v13
	mad v14, v11, s12, v14

	add v12, v12, s5
	add v13, v13, s9
	add v14, v14, s13

	// projPixel, overwrites ProjectedPos: v12, v13
	rcp v14, v14
	mov v16, 0.5
	mad v12, v12, v14, v16				// v12: projPixel.x
	mad v13, v13, v14, v16				// v13: projPIxel.y

	// Test whether this new pixel remains within bounds.
	// P2: Test the case that we must continue.
	// Update P0 with the "break out/not commit" predicate.
	// Update P1 with the "continue, commit" predicate.
	bufquery.dim_y s1, 6			// XXX: This has been queried before
	sisub s2, s0, 1
	sisub s3, s1, 1
	scvt.i2f s2, s2
	scvt.i2f s3, s3
	add.neg v2, v12, s2
	add.neg v27, v13, s3
	test.le p2, v2
	test.le p3, v27
	pbool.and p2, p2, p3
	test.ge p3, v12
	pbool.and p2, p2, p3
	test.ge p3, v13
	pbool.and p2, p2, p3

	pbool.or p3, p0, p2
	// Update 
	pbool.nand p0, p1, p2
	pbool.and p1, p1, p2

	cpush.if endInvProjPixel
	cmask p3
	mov v12, 0
	mov v13, 0
	mov v0, -2						// cmov could be helpful.
	cpop

endInvProjPixel:
	// Convert to pixel index integers
	cvt.f2i v12, v12				// refPixel.x
	cvt.f2i v13, v13				// refPixel.y

	// Load referenceNormal in v16-v18
	smovssp sc.sd_words, 96				// XXX: superfluous?
	imul v27, v12, 3
	imad v27, v13, 1920, v27

	// Load float3s from inNormal
	movvsp vc.mem_idx, v27
	ldgidxit v16, 3

	// If equal to -2., pixel is INVALID
	// p0 contains the "we should break out, not commit" predicate.
	// p1 contains the "we should continue" condition.
	add v17, v16, 2.
	test.nz p2, v17						// p2 Valid
	pbool.or p3, p0, p2  
	pbool.nand p0, p1, p2

	pbool.and p1, p1, p2
	cpush.if endRefNormal
	cmask p3
	mov v0, -3							// cmov could help here.
	cpop

endRefNormal:
	iadd v27, v27, 1
	movvsp vc.mem_idx, v27
	ldgidxit v17, 3						// v17: referenceNormal.y

	iadd v27, v27, 1
	movvsp vc.mem_idx, v27
	ldgidxit v18, 3						// v18: referenceNormal.z

	// Load refVertex, calculate difference with projectedVertex
	// refVertex in v19-v21
	isub v27, v27, 2
	movvsp vc.mem_idx, v27
	ldgidxit v19, 2						// v19: refVertex.x

	iadd v27, v27, 1
	movvsp vc.mem_idx, v27
	ldgidxit v20, 2						// v20: refVertex.y

	iadd v27, v27, 1
	movvsp vc.mem_idx, v27
	ldgidxit v21, 2						// v21: refVertex.z	

	// Substitute with difference
	add.neg v19, v19, v9				// v19: diff.x
	add.neg v20, v20, v10				// v20: diff.y
	add.neg v21, v21, v11				// v21: diff.z

	// v22: length
	mul v22, v19, v19
	mad v22, v20, v20, v22
	mad v22, v21, v21, v22
	rsqrt v22, v22
	rcp v22, v22

	// If larger than diff threshold, pixel is INVALID
	// p0 contains the "we should break out, not commit" predicate.
	// p1 contains the "we should continue" condition.
	sldg s14, 5, 2

	// From here on there are no more global loads. Break out all threads that
	// are currently doing useless stuff. This makes subsequent conditions
	// a lot easier to deal with.
	brk p0

	add.neg v23, v22, s14
	test.g p0, v23						// v22 > dist_threshold, invalid
	test.le p1, v23						// v22 valid
	cpush.if endDistThreshold
	cmask p1
	mov v0, -4							// cmov could help here.
	brk p0
	cpop

endDistThreshold:
	// Calculate projectedNormal into v6-v8, overwriting inVertexPixel
	smovssp sc.sd_words, 11
	sldsp s2, 1

	mul v6, v3, s2
	mul v7, v3, s6
	mul v8, v3, s10

	mad v6, v4, s3, v6
	mad v7, v4, s7, v7
	mad v8, v4, s11, v8

	mad v6, v5, s4, v6
	mad v7, v5, s8, v7
	mad v8, v5, s12, v8

	// Calculate dot product of projectedNormal and refNormal into v19
	mul v22, v6, v16
	mad v22, v7, v17, v22
	mad v22, v8, v18, v22

	add.neg v22, v22, s15

	test.g p0, v22						// p2 Valid
	test.le p1, v22
	cpush.if endNormalThreshold
	cmask p0
	mov v0, -5							// cmov could help here.
	brk p1
	cpop

endNormalThreshold:
	mov v0, 1
	// Dot product of refNormal and Diff
	mul v31, v16, v19
	mad v31, v17, v20, v31
	mad v31, v18, v21, v31

	// Prepare the cross product of the projectedVertex and referenceNormal
	// Stick final result into into v28-v30
	// Cross product of (v9, v10, v11) X (v16, v17, v18)
	// Stick the negative in v13-v15, then mad the final results into them
	mul.neg v28, v11, v17
	mul.neg v29, v9, v18
	mul.neg v30, v10, v16

	mad v28, v10, v18, v28
	mad v29, v11, v16, v29
	mad v30, v9, v17, v30

	cpop // pop the break mask off if it's still on the stack.

store:
	mov v2, vc.lid_x
	mov v3, vc.lid_y
	imul v2, v2, 8
	imad v3, v3, 256, v2

	// Store status code	
	movvsp vc.mem_idx v3
	movvsp vc.mem_data v0
	stspbidx 0

	// Store error
	iadd v3, v3, 1
	movvsp vc.mem_idx v3
	movvsp vc.mem_data v31
	stspbidx 0

	// Store referenceNormal
	iadd v3, v3, 1
	movvsp vc.mem_idx v3
	movvsp vc.mem_data v16
	stspbidx 0

	iadd v3, v3, 1
	movvsp vc.mem_idx v3
	movvsp vc.mem_data v17
	stspbidx 0

	iadd v3, v3, 1
	movvsp vc.mem_idx v3
	movvsp vc.mem_data v18
	stspbidx 0

	// And the cross product in v28, v30
	iadd v3, v3, 1
	movvsp vc.mem_idx v3
	movvsp vc.mem_data v28
	stspbidx 0

	iadd v3, v3, 1
	movvsp vc.mem_idx v3
	movvsp vc.mem_data v29
	stspbidx 0

	iadd v3, v3, 1
	movvsp vc.mem_idx v3
	movvsp vc.mem_data v30
	stspbidx 0

	// Store the final result into the output buffer
	smov s0, sc.wg_off_x
	smov s1, sc.wg_off_y
	simul s0, s0, 8
	stg2sptile 0, 6, s0, s1

	exit
