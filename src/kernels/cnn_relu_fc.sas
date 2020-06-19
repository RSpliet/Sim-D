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
	0 0x0    4096 1    f "data/cnn_relu/in_large.bin"
	1 0x4000 4096 1    f "data/cnn_relu/biases_large.bin"
	2 0x8000 4096 4096 f "data/cnn_relu/weights_large.bin"
	3 0x4008000 4096 1

.sp_data
    0 0x0    4096 1    // inputs

.text
	ldg2sptile 0, 0
	smov s0, 4096
	ldglin v0, 1

loop:
	sisub s0, s0, 1
	sldsp s1, 0, s0
	
	ldglin v1, 2, 0, s0
	mad v0, v1, s1, v0

	#bound branchcycle 4095 1 0
	sicj.nz loop, s0
	
	max v0, v0, 0.f

	stglin v0, 3 
	exit
