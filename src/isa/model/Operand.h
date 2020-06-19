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

#ifndef COMPUTE_MODEL_ISA_OPERAND_H_
#define COMPUTE_MODEL_ISA_OPERAND_H_

#include <systemc>

#include "model/reg_read_req.h"

using namespace sc_core;
using namespace sc_dt;
using namespace std;

namespace isa_model {

class BB;

/** Type of an operand. */
typedef enum {
	OPERAND_NONE = 0,
	OPERAND_REG,
	OPERAND_IMM,
	OPERAND_BRANCH_TARGET,
	OPERAND_SENTINEL
} OperandType;

/** Single operand for an instruction */
class Operand {
private:
	/** Operand type */
	OperandType type;

	/** Register type */
	RegisterType rtype;

	/** Payload. Imm: value, otherwise reg index */
	unsigned int payload;

	/** Branch target label. */
	string branch_target;

	/** Resolved branch target BB. */
	BB *target_bb;

public:
	/**
	 * Constructor for Register operand.
	 * @param rt Register type
	 * @param idx Payload. For SGPR, VGPR, PR: row.
	 */
	Operand(RegisterType rt, unsigned int idx);

	/**
	 * Constructor for immediate operand.
	 * @param imm Immediate value
	 */
	Operand(unsigned int imm);

	/** Empty constructor, creates an invalid operand. */
	Operand();

	/** Constructor from string.
	 * @param s String containing a textual representation of the operand.*/
	Operand(string &s);

	/**
	 * Returns the type of the operand.
	 * @return Type of operand.
	 */
	OperandType getType(void) const;

	/**
	 * Return the type of the register, in case this is a register operand
	 * @return Type of register.
	 */
	RegisterType getRegisterType(void) const;

	/**
	 * For register operands, return the index into the corresponding
	 * register file.
	 * @return The index of the operand.
	 */
	unsigned int getIndex(void) const;

	/**
	 * Return a default register associated with this operand.
	 * @return A default register associated with this operand.
	 */
	template<unsigned int COLS>
	Register<COLS> getRegister() const
	{
		return Register<COLS>(0,rtype,payload,0);
	}

	/**
	 * Return the register associated with this operand, fill in the blanks.
	 * @param wg Work-group slot for this register.
	 * @param col Column (warp) for this register.
	 */
	template<unsigned int COLS>
	Register<COLS> getRegister(sc_uint<1> wg,
			sc_uint<const_log2(COLS)> col) const
	{
		assert(type != OPERAND_BRANCH_TARGET || branch_target_resolved());

		if (type == OPERAND_REG)
			return Register<COLS>(wg,rtype,payload,col);
		else
			return Register<COLS>(wg,rtype,0,0);
	}

	/**
	 * For value based operands (immediate), return the absolute value.
	 * @return The value of the immediate operand.
	 */
	unsigned int getValue();

	/** Return the branch target for this op */
	string getBranchTarget() const;

	/**
	 * Return a pointer to the target BB.
	 * @return Pointer to the target BB for this operand in case this is
	 * 	   a branch target, nullptr otherwise.
	 */
	BB *getTargetBB() const;

	/**
	 * Resolve a branch target.
	 * @param bb BB reached by this branch target operand.
	 *  */
	void resolveBranchTarget(BB *bb);

	/**
	 * Return true iff this branch target has been resolved.
	 * @return True iff this branch target is resolved. */
	bool branch_target_resolved(void) const;

	/**
	 * Validator.
	 * @return true iff this operand is valid.
	 */
	bool isValid(void);

	/**
	 * Return true iff the operand is stored as a vector
	 * @return True iff this operand refers a vector destination.
	 */
	bool isVectorType(void) const;

	/**
	 * Return true iff the operand is a CMASK register
	 * @return True iff the operand targets a CMASK register.
	 */
	bool modifiesCMASK(void) const;

	/**
	 * SystemC mandatory trace function.
	 * @param tf Trace file
	 * @param v Operand to trace
	 * @param NAME Name of the signal conveying this operand.
	 */
	inline friend void sc_trace(sc_trace_file *tf, const Operand &v,
		const std::string & NAME ) {
	      sc_trace(tf,v.type, NAME + ".type");
	}

	/**
	 * Comparator.
	 * @param v Object to compare to.
	 * @return true iff objects are equal.
	 */
	inline bool
	operator==(const Operand &v) const
	{
		if (type != v.type)
			return false;

		switch (type) {
		case OPERAND_IMM:
		case OPERAND_REG:
			return payload == v.payload;
			break;
		case OPERAND_BRANCH_TARGET:
			if (target_bb != v.target_bb)
				return false;

			if (branch_target_resolved())
				return payload == v.payload;
			else
				return branch_target == v.branch_target;

			break;
		case OPERAND_NONE:
		default:
			return true;
			break;
		}
	}

	/**
	 * Inverse comparator.
	 * @param v Object to compare to
	 * @return false iff objects are equal.
	 */
	inline bool operator!=(const Operand &v) const {
		return !(*this == v);
	}

	/** Comparator against Operands.
	 *
	 * Of course the level of detail to compare is limited. Operands don't
	 * specify columns or warps. We'll still have to compare those by hand.
	 * However, for unit testing this comparison is pretty useful.
	 * @param v Object to compare to this.
	 * @return true iff objects are equal.
	 */
	inline bool
	operator==(const AbstractRegister &v) const
	{
		switch (type) {
		case OPERAND_REG:
			return payload == v.row;
			break;
		case OPERAND_IMM:
			return v.type == REGISTER_IMM;
			break;
		case OPERAND_NONE:
			return v.type == REGISTER_NONE;
			break;
		case OPERAND_BRANCH_TARGET:
			if (!branch_target_resolved())
				return false;

			return (v.type == REGISTER_IMM && payload == v.row);
			break;
		default:
			break;
		}

		return false;
	}

	/**
	 * Incomparator.
	 * @param v Object to compare to this.
	 * @return false iff objects are equal.
	 */
	inline bool
	operator!=(const AbstractRegister &v) const
	{
		return !(*this == v);
	}

	/**
	 * SystemC mandatory print stream operation
	 * @param os Output stream
	 * @param v Operand to print
	 * @return Output stream
	 */
	inline friend std::ostream&
	operator<<(std::ostream &os, Operand const &v)
	{
		switch (v.type) {
		case OPERAND_REG:
			AbstractRegister::print(os, v.rtype, v.payload);
			break;
		case OPERAND_IMM:
			os << "imm(" << v.payload << ")";
			break;
		case OPERAND_BRANCH_TARGET:
			if (v.branch_target_resolved())
				os << v.payload;
			else
				os << v.branch_target;
			break;
		default:
			os << "ERROR";
		}

		return os;
	}

	/**
	 * Assignment copy-constructor
	 * @param v Operand to take initialisation parameters from.
	 * @return Reference to new operand &this.
	 */
	inline Operand&
	operator=(Operand const &v)
	{
		type = v.type;
		rtype = v.rtype;
		payload = v.payload;
		target_bb = v.target_bb;
		branch_target = v.branch_target;

		return *this;
	}
};

}

#endif /* COMPUTE_MODEL_ISA_OPERAND_H_ */
