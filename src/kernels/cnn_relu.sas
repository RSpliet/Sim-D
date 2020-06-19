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
	0 0x0 256 512 d "data/cnn_relu/cnn_relu.txt"
	1 0xa0000 16 1 d "data/cnn_relu/cnn_relu_biases.txt"
	2 0x140000 256 512

.text
	sldg s0, 1, 2
	ldglin v1, 0
	ldglin v2, 0, 0, 256
	add v1, v1, s0
	max v1 v1, 0
	add v2, v2, s1
	max v2, v2, 0
	stglin v1, 2
	stglin v2, 2, 0, 256 
	exit
