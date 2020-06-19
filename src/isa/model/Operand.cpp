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

#include <stdexcept>
#include <limits>

#include "isa/model/Operand.h"
#include "isa/model/BB.h"
#include "util/parse.h"

using namespace compute_model;
using namespace isa_model;
using namespace std;

static const unsigned int maxPayload[REGISTER_SENTINEL] = {
		[REGISTER_VGPR] = 63,
		[REGISTER_SGPR] = 31,
		[REGISTER_PR] = 3,
		[REGISTER_IMM] = std::numeric_limits<unsigned int>::max(),
		[REGISTER_VSP] = VSP_SENTINEL - 1,
		[REGISTER_SSP] = SSP_SENTINEL - 1,
};

typedef enum {
	S_INIT = 0,
	S_V,
	S_V_ID,
	S_VC,
	S_VC_ID,
	S_S,
	S_S_ID,
	S_SC,
	S_SC_ID,
	S_P,
	S_P_ID,
	S_LABEL,
	S_SENTINEL
} Operand_Parse_FSM;

static const RegisterType stateToRegType[S_SENTINEL] = {
		[S_INIT] = REGISTER_NONE,
		[S_V] = REGISTER_VGPR,
		[S_V_ID] = REGISTER_VGPR,
		[S_VC] = REGISTER_VSP,
		[S_VC_ID] = REGISTER_VSP,
		[S_S] = REGISTER_SGPR,
		[S_S_ID] = REGISTER_SGPR,
		[S_SC] = REGISTER_SSP,
		[S_SC_ID] = REGISTER_SSP,
		[S_P] = REGISTER_PR,
		[S_P_ID] = REGISTER_PR,
		[S_LABEL] = REGISTER_NONE,
};

Operand::Operand(RegisterType rt, unsigned int idx)
: type(OPERAND_REG), rtype(rt), payload(idx), branch_target(""),
  target_bb(nullptr)
{
	if (idx > maxPayload[rt])
		throw invalid_argument("Payload out of bounds");
}

Operand::Operand(unsigned int p)
: type(OPERAND_IMM), rtype(REGISTER_IMM), payload(p), branch_target(""),
  target_bb(nullptr)
{
}

Operand::Operand()
: type(OPERAND_NONE), rtype(REGISTER_NONE), payload(0), branch_target(""),
  target_bb(nullptr)
{
}

Operand::Operand(string &s) : target_bb(nullptr)
{
	bfloat bf;
	unsigned int ridx;
	string id;
	string subop;
	Operand_Parse_FSM state = S_INIT;
	unsigned int i;

	skip_whitespace(s);

	/** Are we reading an immediate value? */
	if (read_imm(s, bf)) {
		type = OPERAND_IMM;
		rtype = REGISTER_IMM;
		payload = bf.b;
		goto exit_op;
	}

	/* Register description or branch target label.
	 * In correspondence with the register print function:
	 * - vcXX or vc.YYY -> VSP
	 * - scXX or sx.YYY -> ssp
	 * - rXX -> SGPR
	 * - vXX -> VGPR
	 * - pXX -> PR
	 * - Anything else is a branch target.
	 * First figure out what this is. */
	if (read_id(s, id, true)) {
		for (i = 0; i < id.size(); i++) {
			switch (state) {
			case S_INIT:
				switch (id[i]) {
				case 'v':
					state = S_V;
					break;
				case 's':
					state = S_S;
					break;
				case 'p':
					state = S_P;
					break;
				default:
					state = S_LABEL;
					break;
				}
				break;
			case S_V:
				if (is_num(id[i]))
					state = S_V_ID;
				else if (id[i] == 'c')
					state = S_VC;
				else
					state = S_LABEL;
				break;
			case S_S:
				if (is_num(id[i]))
					state = S_S_ID;
				else if (id[i] == 'c')
					state = S_SC;
				else
					state = S_LABEL;
				break;
			case S_P:
				if (is_num(id[i]))
					state = S_P_ID;
				else
					state = S_LABEL;
				break;
			case S_VC:
				if (is_num(id[i]))
					state = S_VC_ID;
				else
					state = S_LABEL;
				break;
			case S_SC:
				if (is_num(id[i]))
					state = S_SC_ID;
				else
					state = S_LABEL;
				break;
			case S_V_ID:
			case S_S_ID:
			case S_SC_ID:
			case S_VC_ID:
			case S_P_ID:
				if (!is_num(id[i]))
					state = S_LABEL;
				break;
			case S_LABEL:
			default:
				break;
			}

			if (state == S_LABEL)
				break;
		}

		switch (state) {
		case S_INIT:
			throw invalid_argument("Unknown error");
			break;
		case S_LABEL:
			if (is_reserved_const(id)) {
				type = OPERAND_IMM;
				rtype = REGISTER_IMM;
				payload = reserved_const(id);
			} else {
				if (id[0] == '-')
					throw invalid_argument("Invalid branch "
							"target "+ id);

				type = OPERAND_BRANCH_TARGET;
				rtype = REGISTER_IMM;
				branch_target = id;
			}

			goto exit_op;
			break;
		case S_V:
		case S_S:
		case S_P:
			throw invalid_argument("Invalid register specification "
					"\"" + id + "\"");
			break;
		case S_VC:
			if(!read_char(s, '.'))
				throw invalid_argument("Invalid register"
						" specification \"" + id +
						"\"");

			if (read_id(s,subop)) {
				for (i = 0; i < VSP_SENTINEL; i++) {
					if (vsp_str[i].alias == subop) {
						ridx = i;
						break;
					}
				}

				if (i == VSP_SENTINEL)
					throw invalid_argument("Invalid register"
						" suboperand for \"" + id +
						"\": " + subop);
			} else if (!read_uint(s,ridx)){
				throw invalid_argument("Invalid register"
					" suboperand for \"" + id + "\"");

			}

			break;
		case S_SC:
			if (!read_char(s, '.'))
				throw invalid_argument("Invalid register"
						" specification \"" + id +
						"\"");

			if (read_id(s,subop)) {
				for (i = 0; i < SSP_SENTINEL; i++) {
					if (ssp_str[i].alias == subop) {
						ridx = i;
						break;
					}
				}

				if (i == SSP_SENTINEL)
					throw invalid_argument("Invalid register"
						" suboperand for \"" + id +
						"\": " + subop);
			} else if (!read_uint(s,ridx)){
				throw invalid_argument("Invalid register"
					" suboperand for \"" + id + "\"");

			}
			break;
		case S_VC_ID:
		case S_SC_ID:
			id = id.substr(1);
			/* Fall-through */
		case S_S_ID:
		case S_V_ID:
		case S_P_ID:
			id = id.substr(1);
			if(!read_uint(id, ridx))
				throw invalid_argument("Unknown error");
			break;
		default:
			throw invalid_argument("Invalid state reached");
			break;
		}

		type = OPERAND_REG;
		rtype = stateToRegType[state];
		payload = ridx;

		if (payload > maxPayload[rtype])
			throw invalid_argument("Register index \"" +
					to_string(payload) +
					"\" out of bounds");

exit_op:
		/** If there's a comma separator, remove it. Not mandatory */
		read_char(s, ',');
		return;
	}

	type = OPERAND_NONE;
	rtype = REGISTER_NONE;
	payload = 0;
}

OperandType
Operand::getType() const
{
	return type;
}

RegisterType
Operand::getRegisterType() const
{
	return rtype;
}

unsigned int
Operand::getIndex() const
{
	switch (type) {
	case OPERAND_REG:
		return payload;
	default:
		/* throw new OperandTypeException(); */
		break;
	}

	return 0;
}

unsigned int
Operand::getValue()
{
	switch (type) {
	case OPERAND_IMM:
		return payload;
	case OPERAND_BRANCH_TARGET:
		if (branch_target_resolved())
			return payload;
		/* fall-through */
	default:
		/* throw new OperandTypeException(); */
		break;
	}

	return 0;
}

string
Operand::getBranchTarget() const
{
	return branch_target;
}

BB *
Operand::getTargetBB() const
{
	return target_bb;
}

void
Operand::resolveBranchTarget(BB *bb)
{
	target_bb = bb;
	payload = bb->get_pc_uint();
}

bool
Operand::branch_target_resolved(void) const
{
	return target_bb != nullptr;
}

bool
Operand::isValid()
{
	if (type == OPERAND_SENTINEL)
		return false;

	return true;
}

bool
Operand::isVectorType(void) const
{
	if (type == OPERAND_REG)
		return AbstractRegister::isVectorType(rtype);

	return false;
}

bool
Operand::modifiesCMASK(void) const
{
	return (type == OPERAND_REG && rtype == REGISTER_VSP && payload < 4);
}
