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

#ifndef UTIL_PARSE_H
#define UTIL_PARSE_H

#include "util/constmath.h"

using namespace std;
using namespace simd_util;

inline bool
is_whitespace(char c)
{
	return (c == ' ' || c == '\t');
}

inline bool
is_num(char c)
{
	return (c >= 48 && c < 58);
}

/** True iff the provided character is valid inside a label.
 *
 * Currently [A-Za-z0-9_]. */
inline bool
valid_id_char(char c)
{
	/* 0-9 */
	if (is_num(c))
		return true;

	/* A-Z, a-z */
	if ((c & ~0x20) >= 65 && (c & ~0x20) < 91)
		return true;

	if (c == '_')
		return true;

	return false;
}

/** True iff the string can be ignored altogether by the parser. Either
 * whitespace or comment.
 *
 * @param s String to scan.
 */
bool is_whitespace(string &s);

/** Check whether an identifier is a reserved constant in OpenCL.
 * @param s Const name to check.
 * @return true iff s is the name of a reserved constant. */
bool is_reserved_const(const string &s);

/** Retreive a binary representation of the requested const value.
 * @param s Constant requested.
 * @return 32-bit "unsigned int" containing the value for the requested const.*/
unsigned int reserved_const(const string &s);

/** Extract an identifier from the remainder of the string */
string extract_id(string s);

void skip_whitespace(string &s);

bool read_char(string &s, char c);

bool read_int(string &s, int &ival);

/** Read an unsigned integer from the input string, advance input
 * string if successful. */
bool read_uint(string &s, unsigned int &ival);

bool read_imm(string &s, bfloat &bf);

/** An ID-ish token starts with a latin letter or _, followed by a
 * sequence of [A-Za-z0-9_]
 */
bool read_id(string &s, string &word, bool allow_neg = false);

/** Read a path of the form "blah/blah/blah.txt" */
bool read_path(string &s, string &path);

string escapeLaTeX(const string &s);

#endif /* UTIL_PARSE_H */
