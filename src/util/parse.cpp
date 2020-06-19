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

#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>

#include "util/parse.h"
#include "util/constmath.h"

using namespace std;

typedef enum {
	CONST_FLT,
	CONST_INT,
} const_type;

/** (Incomplete) list of OpenCL defined constants for 32-bit platforms. */
const static unordered_map<string, pair<unsigned int,const_type> > consts =
{
	{"FLT_DIG", {6, CONST_INT}},
	{"FLT_MANT_DIG", {24, CONST_INT}},
	{"FLT_MAX_10_EXP", {38, CONST_INT}},
	{"FLT_MAX_EXP", {128, CONST_INT}},
	{"FLT_MIN_10_EXP", {-37, CONST_INT}},
	{"FLT_MIN_EXP", {-128, CONST_INT}},
	{"FLT_RADIX", {2, CONST_INT}},
	{"FLT_MAX", {0x7f7fffff, CONST_FLT}},
	{"FLT_MIN", {0x00800000, CONST_FLT}},
	{"FLT_EPSILON", {0x34000000, CONST_FLT}},
	{"M_PI_F", {0x40490fdb, CONST_FLT}},
	{"M_2PI_F", {0x40c90fdb, CONST_FLT}},
	{"M_E_F", {0x402df854, CONST_FLT}},
};

bool
is_whitespace(string &s)
{
	unsigned int i;

	for (i = 0; i < s.size(); i++) {
		switch (s[i]) {
		case '\t':
		case ' ':
			continue;
		case '/':
			if (i+1 < s.size())
				return s[i+1] == '/';

			return false;
			break;
		default:
			return false;
			break;
		}
	}

	return true;
}

bool
is_reserved_const(const string &s)
{
	unordered_map<string, pair<unsigned int,const_type> >::const_iterator res;

	if (s[0] == '-') {
		res = consts.find(s.substr(1));
	} else {
		res = consts.find(s);
	}

	return (res != consts.end());
}

unsigned int
reserved_const(const string &s)
{
	unordered_map<string, pair<unsigned int,const_type> >::const_iterator res;
	bool neg;
	unsigned int retval = 0;

	if (s[0] == '-') {
		neg = true;
		res = consts.find(s.substr(1));
	} else {
		neg = false;
		res = consts.find(s);
	}

	if (res != consts.end()) {
		retval = res->second.first;

		if (neg) {
			if (res->second.second == CONST_FLT)
				retval ^= 0x80000000;
			else
				retval = -retval;
		}
	}

	return retval;
}

/** Extract an identifier from the remainder of the string */
string
extract_id(string s)
{
	unsigned int i;

	for (i = 0; i < s.size(); i++)
		if (!valid_id_char(s[i]))
			break;

	return s.substr(0,i);
}

void
skip_whitespace(string &s)
{
	unsigned int i;

	for (i = 0; i < s.size(); i++) {
		if (s[i] == '/' && i + 1 < s.size() && s[i+1] == '/') {
			i = s.size();
			break;
		}

		if (!is_whitespace(s[i]))
			break;
	}

	s = s.substr(i);
}

bool
read_char(string &s, char c)
{
	skip_whitespace(s);
	if (s.size() == 0 || s[0] != c)
		return false;

	s = s.substr(1);
	return true;
}

/** Read an unsigned integer from the input string, advance input
 * string if successful. */
bool
read_int(string &s, int &ival)
{
	string::size_type sz;

	skip_whitespace(s);

	try {
		ival = stoi(s, &sz, 0);
	} catch (exception &e) {
		return false;
	}

	s = s.substr(sz);

	return true;
}

/** Read an unsigned integer from the input string, advance input
 * string if successful. */
bool
read_uint(string &s, unsigned int &ival)
{
	string::size_type sz;

	skip_whitespace(s);

	if (s.size() > 0 && s[0] == '-')
		return false;

	try {
		ival = stoi(s, &sz, 0);
	} catch (exception &e) {
		return false;
	}

	s = s.substr(sz);

	return true;
}

bool
read_imm(string &s, bfloat &bf)
{
	unsigned int i;
	enum {
		S_NONE,
		S_U_ZERO,
		S_UINT,
		S_INT,
		S_UHEX,
		S_UHEX_F,
		S_FLT,
		S_FLT_DOT,
		S_NO_IMM
	} state = S_NONE;
	bool last = false;
	string::size_type sz;

	skip_whitespace(s);

	/* Pass 1: figure out the type of what we're reading */
	for (i = 0; i < s.size(); i++) {
		switch (state) {
		case S_NONE:
			if (s[i] == '-')
				state = S_INT;
			else if (s[i] == '0')
				state = S_U_ZERO;
			else if ((s[i] & ~0x20) >= 65 && (s[i] & ~0x20) < 71)
				state = S_UHEX;
			else if (is_num(s[i]))
				state = S_UINT;
			else if (s[i] == '.')
				state = S_FLT;
			else
				return false;
			break;
		case S_U_ZERO:
			if (s[i] == 'x') {
				state = S_UHEX;
			} else if (s[i] == '.') {
				state = S_FLT;
			} else if (s[i] == 'f') {
				state = S_UHEX_F;
			} else if ((s[i] & ~0x20) >= 65 && (s[i] & ~0x20) < 71) {
				state = S_UHEX;
			} else if (is_num(s[i])) {
				state = S_UINT;
			} else if (s[i] == ',' || is_whitespace(s[i])) {
				state = S_UINT;
				last = true;
			} else {
				state = S_NO_IMM;
			}
			break;
		case S_UINT:
			if (s[i] == '.')
				state = S_FLT;
			else if ((s[i] & ~0x20) >= 65 && (s[i] & ~0x20) < 71)
				state = S_UHEX;
			else if (is_whitespace(s[i]) || s[i] == ',')
				last = true;
			else if (!is_num(s[i]))
				state = S_NO_IMM;
			break;
		case S_INT:
			if (s[i] == '.')
				state = S_FLT;
			else if (is_whitespace(s[i]) || s[i] == ',')
				last = true;
			else if (!is_num(s[i]))
				state = S_NO_IMM;
			break;
		case S_UHEX:
			if (is_whitespace(s[i]) || s[i] == ',')
				last = true;
			else if (!is_num(s[i]) &&
			    ((s[i] & ~0x20) < 65 || (s[i] & ~0x20) >= 71))
				state= S_NO_IMM;
			break;
		case S_UHEX_F:
			if (is_whitespace(s[i]) || s[i] == ',') {
				state = S_FLT;
				last = true;
			} else if (is_num(s[i]) ||
				 ((s[i] & ~0x20) >= 65 && (s[i] & ~0x20) < 71)) {
				state = S_UHEX;
			} else {
				state = S_NO_IMM;
			}
			break;
		case S_FLT:
			if (s[i] == '.') {
				state = S_FLT_DOT;
				break;
			}
			/* fall-through */
		case S_FLT_DOT:
			if (s[i] == 'f') {
				i++;
				last = true;
			} else if (is_whitespace(s[i]) || s[i] == ',') {
				last = true;
			} else if (!is_num(s[i])) {
				state = S_NO_IMM;
			}

			break;
		default:
			break;
		}

		if (state == S_NO_IMM || last)
			break;
	}

	/* Pass 2: use regular C++ helpers to convert the data */
	switch (state) {
	case S_UINT:
	case S_INT:
	case S_UHEX:
	case S_UHEX_F:
	case S_U_ZERO:
		bf.b = stoi(s.substr(0,i), &sz, 0);
		break;
	case S_FLT_DOT:
	case S_FLT:
		bf.f = stof(s.substr(0,i), &sz);
		break;
	default:
		return false;
		break;
	}

	s = s.substr(sz);
	return true;
}

bool
read_path(string &s, string &path)
{
	unsigned int i;
	string p;
	enum {
		S_NONE,
		S_SLASH,
		S_PATH,
		S_FWSLASH,
		S_PATH_QUOTE,
		S_PATH_END,
		S_QUOTE_FWSLASH,
		S_COMMENT,
	} state = S_NONE;

	skip_whitespace(s);

	for (i = 0; i < s.size(); i++) {
		switch (state) {
		case S_NONE:
			if (s[i] == '"') {
				state = S_PATH_QUOTE;
				break;
			}

			if (s[i] == '/') {
				state = S_SLASH;
				break;
			}

			p += s[i];
			state = S_PATH;
			break;
		case S_SLASH:
			if (s[i] == '/') {
				state = S_COMMENT;
				break;
			}

			state = S_PATH;
			p += "/";
			/* Fall-through */
		case S_PATH:
			if (is_whitespace(s[i])) {
				state = S_PATH_END;
				break;
			}

			if (s[i] == '\\') {
				state = S_FWSLASH;
				break;
			}

			p += s[i];

			break;
		case S_FWSLASH:
			if (s[i] == '\\')
				p += '\\';

			p += s[i];
			state = S_PATH;

			break;
		case S_PATH_QUOTE:
			if (s[i] == '"') {
				i++;
				state = S_PATH_END;
				break;
			}

			if (s[i] == '\\') {
				state = S_QUOTE_FWSLASH;
				break;
			}

			p += s[i];

			break;
		case S_QUOTE_FWSLASH:
			if (s[i] != '"' && s[i] != '\\')
				p += "\\";

			p += s[i];
			state = S_PATH_QUOTE;

			break;
		case S_PATH_END:
		case S_COMMENT:
		default:
			break;
		}

		if (state == S_PATH_END || state == S_COMMENT)
			break;
	}

	switch (state) {
	case S_COMMENT:
		path = "";
		s = "";
		return false;
	case S_PATH_QUOTE:
		throw invalid_argument("Missing closing quotation mark");
		return false;
	case S_NONE:
		return false;
	default:
		path = p;
		s = s.substr(i);
		break;
	}

	return true;
}

/** An ID-ish token starts with a latin letter or _, followed by a
 * sequence of [A-Za-z0-9_]
 */
bool
read_id(string &s, string &word, bool allow_neg)
{
	unsigned int i;

	skip_whitespace(s);

	if (s.size() == 0)
		return false;

	if ((!valid_id_char(s[0]) && (!allow_neg || s[0] != '-')) || is_num(s[0]))
		return false;

	for (i = 1; i < s.size(); i++)
		if (!valid_id_char(s[i]))
			break;

	word = s.substr(0,i);
	s = s.substr(i);
	return true;
}

/** Automatically escape underscores for LaTeX string.
 * @param s String to escape underscores in.
 * @return new escaped string. */
string
escapeLaTeX(const string &s)
{
	string es;
	unsigned int i;

	for (i = 0; i < s.size(); i++) {
		if (s[i] == '_')
			es += "\\";

		es += s[i];
	}

	return es;
}
