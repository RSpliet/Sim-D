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

#include "model/Buffer.h"

using namespace simd_model;

Buffer::Buffer(sc_uint<32> a, sc_uint<32> dim_x, sc_uint<32> dim_y)
: valid(true), addr(a)
{
	dims[0] = dim_x;
	dims[1] = dim_y;
}

sc_uint<32>
Buffer::getAddress(void) const
{
	return addr;
}

sc_uint<32>
Buffer::get_dim_x(void) const
{
	return dims[0];
}

sc_uint<32>
Buffer::get_dim_y(void) const
{
	return dims[1];
}

ProgramBuffer::ProgramBuffer()
: Buffer(), data_input_file(""), data_input_type(INPUT_NONE) {}

ProgramBuffer::ProgramBuffer(sc_uint<32> a, unsigned int dim_x,
		unsigned int dim_y, buffer_input_type dt, string dif)
: Buffer(a, dim_x, dim_y), data_input_file(dif), data_input_type(dt) {}

bool
ProgramBuffer::hasDataInputFile(void) const
{
	return data_input_type != INPUT_NONE;
}

buffer_input_type
ProgramBuffer::getDataInputType(void) const
{
	return data_input_type;
}

string
ProgramBuffer::getDataInputFile(void) const
{
	return data_input_file;
}

void
ProgramBuffer::setDataInputFile(string dif, buffer_input_type dt)
{
	data_input_file = dif;
	data_input_type = dt;
}

size_t
ProgramBuffer::size(void) const
{
	return dims[0] * dims[1] * 4;
}
