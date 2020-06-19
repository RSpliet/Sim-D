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

#ifndef UTIL_CONSTMATH_H
#define UTIL_CONSTMATH_H

namespace simd_util {
/** Union that will avoid writing ugly casts in code. */
typedef union {
	/** Binary representation */
	unsigned int b;
	/** Floating point representation */
	float f;
} bfloat;

}

constexpr int const_log2(unsigned int word) {
	return word > 1 ? (1 + const_log2(word>>1)) : 0;
};

constexpr bool is_pot(unsigned int word) {
	return (word & (word - 1)) == 0;
}

/* Not really constant, still useful. */
#ifndef div_round_up
#define div_round_up(a,b) (((a) + b - 1) / b)
#endif

#endif /* UTIL_CONSTMATH_H_ */
