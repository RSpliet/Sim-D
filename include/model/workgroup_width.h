/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2020 Roy Spliet, University of Cambridge
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef INCLUDE_MODEL_WORKGROUP_WIDTH_H
#define INCLUDE_MODEL_WORKGROUP_WIDTH_H

namespace simd_model {

/** @todo How does this generalise to configurations with fewer/more threads? */
typedef enum {
	WG_WIDTH_32 = 0,
	WG_WIDTH_64 = 1,
	WG_WIDTH_128 = 2,
	WG_WIDTH_256 = 3,
	WG_WIDTH_512 = 4,
	WG_WIDTH_1024 = 5,
	WG_WIDTH_2048 = 6,
	WG_WIDTH_SENTINEL
} workgroup_width;

}

#endif /* INCLUDE_MODEL_WORKGROUP_WIDTH_H */
