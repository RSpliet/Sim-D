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
  // Input dimension is 224*224*3.
  0 0x0       224   672 d "data/cnn_convolution/in.txt"
  // Kernels are 7*7*3, there's 64 of them
  1 0x1000000   7  1344 d "data/cnn_convolution/kernels.txt"
  2 0x3000000   3     1 d "data/cnn_convolution/constants.txt"
  // Output is 218*218*64
  3 0x2000000 218 13952
  
.sp_data
  // Local kernel
  0 0x0 7 21
  1 0x300 38 38
  
.text
	// One of the challenges in this kernel is the limited number of scalar
	// registers, forcing us to read the kernels line by line. Worst-case each
	// line read takes 85 cycles. This means the kernel read will require
	// 85 * 21 = 1785 cycles versus 111 cycles if the entire kernel is read from
	// DRAM in one go. A scratchpad would reduce execution time if its single
	// word access time is 12 cycles or less, but still increases parallelism
	// if it is larger.
	//
	// Another problem is that reading line-by-line will put s1 values in
	// s1 consecutive registers. We can't indirectly address these registers
	// in our program, hence we'd have to unroll our loop by a "dynamic" factor
	// s1. Although our set-up is somewhat contrived in the sense that we don't
	// really use OpenCL APIs to control buffer sizes but rather hard-code
	// buffer dimensions in this assembly file rather, unrolling by such a
	// factor seems like a wrong thing to do because we'd sacrifice the
	// generality of the kernel *code*. 
	// This is a shame because it would have been nice to have a comparison
	// between a per-line kernel and a scratchpad-enabled kernel. Maybe we'll
	// unroll for academics sake regardless if time permits, with the clear
	// disclaimer that it's bad coding practice.
	
	// Load kernel parameters from buffer
	sldg s0, 2, 3					// s0: kernelSize
									// s1: inChannels
									// s2: dim_y 
	smov s3, sc.wg_off_y			// Workgroup Y offset
	scvt.f2i s0, s0
	scvt.f2i s1, s1
	scvt.f2i s2, s2
	
	// We launch this kernel in work groups of 32*32. Global dimensions are 
	// 224*224*output channels. Thus integer divide the y WG offset by 224 to
	// get the channel output.
	sidiv s4, s3, 224				// Output channel
	smov s8, sc.wg_off_x			// Workgroup X offset
	mov v0, vc.tid_y
	mov v1, vc.tid_x
	simul s5, s4, s1				// First kernel input channel
	simad s6, s4, 224, s2			// tid_y bound for this output channel
  
  	// Disable threads outside bounds.
	// Program is launched in the y dimension in multiples of 216, such that
	// a workgroup only hits a single channel and to simplify calculating
	// the y-offset below.
	cvt.i2f v2, vc.zero
	simod s9, s3, 224				// Remainder, y-offset within channel.
	smov s3, sc.dim_x
	isub v1, v1, s2
	isub v0, v0, s6
	itest.ge p1, v1
	itest.ge p0, v0
	pbool.or p0, p0, p1
	exit p0
	
	// Load kernels
	simul s7, s5, s0
	bufquery.dim_x s5, 0			// Square channels, dim_x=dim_y.
	smov s10, 0						// inCh
	smov s14, 0						// y in kernel buffer
	ldg2sptile 0, 1, 0, s7			// Load kernels

loop_inCh:
	smov s11, 0						// ky
	
	// Read tile to SP
	ldg2sptile 1, 0, s8, s9
	
loop_ky:
	smov s12, 0						// kx
	
//loop_kx:
	// Load data from tile to vector register
	ldsplin v3, 1, s12, s11
	sldsp s15, 0, s12, s14
	siadd s12, s12, 1				// Increment loop counter early, hide RAW
	mad v2, v3, s15, v2
	
	ldsplin v3, 1, s12, s11
	sldsp s15, 0, s12, s14
	siadd s12, s12, 1				// Increment loop counter early, hide RAW
	mad v2, v3, s15, v2
	
	ldsplin v3, 1, s12, s11
	sldsp s15, 0, s12, s14
	siadd s12, s12, 1				// Increment loop counter early, hide RAW
	mad v2, v3, s15, v2
	
	ldsplin v3, 1, s12, s11
	sldsp s15, 0, s12, s14
	siadd s12, s12, 1				// Increment loop counter early, hide RAW
	mad v2, v3, s15, v2
	
	ldsplin v3, 1, s12, s11
	sldsp s15, 0, s12, s14
	siadd s12, s12, 1				// Increment loop counter early, hide RAW
	mad v2, v3, s15, v2
	
	ldsplin v3, 1, s12, s11
	sldsp s15, 0, s12, s14
	siadd s12, s12, 1				// Increment loop counter early, hide RAW
	mad v2, v3, s15, v2
	
	ldsplin v3, 1, s12, s11
	sldsp s15, 0, s12, s14
	siadd s12, s12, 1				// Increment loop counter early, hide RAW
	mad v2, v3, s15, v2
	
	//sisub s13, s12, s0
	//bound branchcycle 6 1 0
	//sicj.l loop_kx, s13

	siadd s11, s11, 1
	siadd s14, s14, 1				// Input channel
	sisub s13, s11, s0
	#bound branchcycle 6 1 0
	sicj.l loop_ky, s13
	
	siadd s10, s10, 1
	siadd s9, s9, s5				// Next channel
	sisub s13, s10, s1
	#bound branchcycle 2 1 0
	sicj.l loop_inCh, s13

	// Store result
	simul s4, s4, -6
	stglin v2, 3, 0, s4
	
	exit