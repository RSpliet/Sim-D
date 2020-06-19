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
	0 0x0 2048 1 f "data/mri-q/phiR.bin"
	1 0x2000 2048 1 f "data/mri-q/phiI.bin"
	2 0x4000 2048 1

.text
	ldglin v1, 0
	ldglin v2, 1
	mul v1, v1, v1
	mad v2, v2, v2, v1
	stglin v2, 2
	exit