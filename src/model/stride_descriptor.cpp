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

#include <cstdio>
#include <iomanip>

#include "model/stride_descriptor.h"

using namespace std;
using namespace simd_model;

namespace simd_model {

ostream&
operator<<(ostream &os, stride_descriptor const &v)
{
	os << "(0x" << hex << v.addr << "," << dec
			<< v.words << "," << v.period << ","
			<< v.period_count << ") " <<
			(v.write ? "<- " : "-> ");

	switch (v.dst.type) {
	case TARGET_SP:
		os << "SP  " << std::hex <<
			v.dst_offset << std::dec;
		break;
	case TARGET_REG:
	case TARGET_CAM:
		os << "REG " << *(v.dst_reg);
		break;
	default:
		os << "INVALID";
		break;
	}

	os << " T(" << v.ticket << ")";

	return os;
}

}

stride_descriptor::stride_descriptor()
: dst_reg(nullptr), type(STRIDE), dst_period(32), write(false),
  idx_transform(IDX_TRANSFORM_UNIT)
{
	dst.type = TARGET_SP;
	dst.wg = 0;
}

stride_descriptor::stride_descriptor(AbstractRegister &reg)
: type(STRIDE), dst_period(32), write(false), idx_transform(IDX_TRANSFORM_UNIT)
{
	dst_reg = reg.clone();
	dst.type = reg.type == REGISTER_VSP ? TARGET_CAM : TARGET_REG;
	dst.wg = reg.wg;
}

stride_descriptor::stride_descriptor(const stride_descriptor &v)
: ticket(v.ticket), type(v.type), addr(v.addr), words(v.words), period(v.period),
  period_count(v.period_count), dst(v.dst), dst_offset(v.dst_offset),
  dst_period(v.dst_period), dst_off_x(v.dst_off_x), dst_off_y(v.dst_off_y),
  write(v.write), idx_transform(v.idx_transform)
{
	if (v.dst_reg != nullptr)
		dst_reg = v.dst_reg->clone();
	else
		dst_reg = nullptr;
}

stride_descriptor::~stride_descriptor()
{
	if (dst_reg != nullptr) {
		delete dst_reg;
		dst_reg = nullptr;
	}
}

req_dest_type_t
stride_descriptor::getTargetType(void) const
{
	return dst.type;
}

AbstractRegister *
stride_descriptor::getTargetReg(void) const
{
	return dst_reg->clone();
}

stride_descriptor *
stride_descriptor::from_csv_string(const char *csv)
{
	stride_descriptor *desc = nullptr;
	int items_read;

	/* I suspect that sscanf wouldn't play nice with sc_uint. Let's play
	 * it safe and copy manually */
	unsigned int write;
	int addr, sp_offset;
	int words, period, period_count;


	items_read = sscanf(csv, "%x,%i,%i,%i,%x,%u", &addr, &words,
			&period, &period_count, &sp_offset,
			&write);

	if (items_read == 6) {
		desc = new stride_descriptor();

		desc->addr = addr;
		desc->words = words;
		desc->period = period;
		desc->period_count = period_count;
		desc->dst = RequestTarget(0,TARGET_SP);
		desc->dst_offset = sp_offset;
		desc->dst_period = words;
		desc->write = write ? true : false;
	}

	return desc;
}

stride_descriptor *
stride_descriptor::from_csv_file(FILE *csv)
{
	stride_descriptor *desc = nullptr;
	int items_read;

	unsigned int write;
	int addr, sp_offset;
	short int words, period, period_count;

	items_read = fscanf(csv, "%x,%hi,%hi,%hi,%x,%u", &addr, &words,
			&period, &period_count, &sp_offset,
			&write);

	if (items_read == 6) {
		desc = new stride_descriptor();

		desc->addr = addr;
		desc->words = words;
		desc->period = period;
		desc->period_count = period_count;
		desc->dst = RequestTarget(0,TARGET_SP);
		desc->dst_offset = sp_offset;
		desc->write = write ? true : false;
	}

	return desc;
}

