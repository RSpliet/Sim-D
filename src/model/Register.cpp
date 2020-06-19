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

#include "model/Register.h"

using namespace simd_model;

AbstractRegister::AbstractRegister(void) : type(REGISTER_NONE), wg(0), row(0)
{
}

AbstractRegister::AbstractRegister(sc_uint<1> workgroup, RegisterType t,
			sc_uint<const_log2(64)> r)
	: type(t), wg(workgroup), row(r) {}

AbstractRegister *
AbstractRegister::clone(void) const
{
	return new AbstractRegister(wg, type, row);
}

bool
AbstractRegister::isCMASK(void) const
{
	return (type == REGISTER_VSP && row < 4);
}

AbstractRegister::~AbstractRegister(void) {}
