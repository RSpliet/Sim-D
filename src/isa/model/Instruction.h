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

#ifndef COMPUTE_MODEL_ISA_INSTRUCTION_H
#define COMPUTE_MODEL_ISA_INSTRUCTION_H

#include <string>
#include <systemc>

#include "isa/model/Operand.h"
#include "isa/model/Metadata.h"

using namespace sc_core;
using namespace sc_dt;

namespace isa_model {

/** Instruction category. */
typedef enum {
	CAT_ARITH_FP = 0, /**< Single-precision floating point (scalar/vector)*/
	CAT_ARITH_RCPU,   /**< Reciprocal+trigonometry. 1/4th throughput */
	CAT_ARITH_INT,    /**< 32-bit integer arithmetic (scalar/vector) */
	CAT_DATA_COPY,    /**< Data copy and conversion. */
	CAT_LDST,         /**< Scalar load/store */
	CAT_CTRLFLOW,     /**< Scalar/vector control flow */
	CAT_PREDICATE,    /**< Test, result in predicate. */
	CAT_DEBUG,        /**< Debugging instructions for simulation only. */
	CAT_SENTINEL
} ISACategory;

/** Documentation strings for generated LaTeX outputs. */
extern const string cat_str[CAT_SENTINEL];

/** Supported operations. */
typedef enum {
	NOP = 0,
	/* Predicate. */
	OP_TEST,
	OP_ITEST,
	OP_PBOOL,
	/* Control flow. */
	OP_J,
	OP_SICJ,
	OP_BRA,
	OP_CALL,
	OP_CPUSH,
	OP_CMASK,
	OP_CPOP,
	OP_RET,
	OP_BRK,
	OP_EXIT,
	/* FPU. */
	OP_MUL,
	OP_ADD,
	OP_MAD,
	OP_MIN,
	OP_MAX,
	OP_ABS,
	/* Data copy. */
	OP_MOV,
	OP_MOVVSP,
	OP_SMOV,
	OP_SMOVSSP,
	OP_CVT,
	OP_SCVT,
	OP_BUFQUERY,
	/* ALU. */
	OP_IADD,
	OP_ISUB,
	OP_IMUL,
	OP_IMAD,
	OP_IMIN,
	OP_IMAX,
	OP_SHL,
	OP_SHR,
	OP_AND,
	OP_OR,
	OP_XOR,
	OP_NOT,
	OP_SIADD,
	OP_SISUB,
	OP_SIMUL,
	OP_SIMAD,
	OP_SIMIN,
	OP_SIMAX,
	OP_SINEG,
	OP_SIBFIND,
	OP_SSHL,
	OP_SSHR,
	OP_SIDIV,
	OP_SIMOD,
	OP_SAND,
	OP_SOR,
	OP_SNOT,
	/* RCPU/Trio. */
	OP_RCP,
	OP_RSQRT,
	OP_SIN,
	OP_COS,
	/* LD/ST. */
	OP_LDGLIN,
	OP_STGLIN,
	OP_LDGBIDX,
	OP_STGBIDX,
	OP_LDGCIDX,
	OP_STGCIDX,
	OP_LDGIDXIT,
	OP_STGIDXIT,
	OP_LDG2SPTILE,
	OP_STG2SPTILE,
	OP_LDSPLIN,
	OP_STSPLIN,
	OP_LDSPBIDX,
	OP_STSPBIDX,
	OP_SLDG,
	OP_SLDSP,
	/* Debug. */
	OP_DBG_PRINTSGPR,
	OP_DBG_PRINTVGPR,
	OP_DBG_PRINTPR,
	OP_DBG_PRINTCMASK,
	OP_DBG_PRINTTRACE,
	OP_SENTINEL,
} ISAOp;

/** Sub-operations (modifiers) for test instructions. */
typedef enum {
	TEST_EZ = 0,
	TEST_NZ,
	TEST_G,
	TEST_GE,
	TEST_L,
	TEST_LE,
	TEST_SENTINEL
} ISASubOpTEST;

/** Sub-operations (modifiers) for Control stack PUSH. */
typedef enum {
	CPUSH_IF = 0,
	CPUSH_BRK,
	CPUSH_RET,
	CPUSH_SENTINEL
} ISASubOpCPUSH;

/** Sub-operations (modifiers) for boolean predicate operation. */
typedef enum {
	PBOOL_AND = 0,
	PBOOL_OR,
	PBOOL_NAND,
	PBOOL_NOR,
	PBOOL_SENTINEL
} ISASubOpPBOOL;

/** Sub-operations (modifiers) for the conversion operation. */
typedef enum {
	CVT_I2F = 0,
	CVT_F2I,
	CVT_SENTINEL
} ISASubOpCVT;

/** Sub-operations (modifiers) for global linear load/store operations. */
typedef enum {
	LIN_UNIT = 0,
	LIN_VEC2,
	LIN_VEC4,
	LIN_SENTINEL
} ISASubOpLDSTLIN;

/** Sub-operations (modifiers) for debug print CMASK operation. */
typedef enum {
	PRINTCMASK_IF = 0,
	PRINTCMASK_BRK,
	PRINTCMASK_RET,
	PRINTCMASK_EXIT,
	PRINTCMASK_SENTINEL
} ISASubOpPRINTCMASK;

/** Negative-modifier for some floating-point arithmetic operations. */
typedef enum {
	FPU_NORMAL = 0,
	FPU_NEG,
	FPU_SENTINEL,
} ISASubOpFPUMod;

/** Sub-operation for the buffer query operation. */
typedef enum {
	BUFQUERY_DIM_X = 0,
	BUFQUERY_DIM_Y,
	BUFQUERY_SENTINEL
} ISASubOpBUFQUERY;

/** Sub-op union containing modifiers on specific operations. */
typedef union {
	/** Modifier for test operation. */
	ISASubOpTEST test;
	/** Modifier for cpush operation. */
	ISASubOpCPUSH cpush;
	/** Modifier for pbool operation. */
	ISASubOpPBOOL pbool;
	/** Modifier for cvt op. */
	ISASubOpCVT cvt;
	/** Modifier vor ldst linear op. */
	ISASubOpLDSTLIN ldstlin;
	/** Modifier for printcmask debug instruction. */
	ISASubOpPRINTCMASK printcmask;
	/** Modifiers for the FPU operations add, mad... */
	ISASubOpFPUMod fpumod;
	/** Modifiers for buffer query */
	ISASubOpBUFQUERY bufquery;
	/** Raw unsigned int value, derived from a string lookup */
	unsigned int raw;
} ISASubOp;

/**
 * Representation of a single instruction.
 */
class Instruction
{
private:
	/** Operation. */
	ISAOp op;
	/** Sub-operation. */
	ISASubOp subop;
	/** Destination operand. */
	Operand dst;

	/** Number of source operands for this operation (0-3). */
	unsigned int srcs;
	/** Array of source operands. */
	Operand src[3];

	/** Indicates whether this instance was killed in the pipeline */
	bool dead;

	/** Indicates whether the destination operand lives on the scoreboard */
	bool on_sb;

	/** Indicates whether a control stack operation is counted on the
	 * scoreboard */
	bool on_cstack_sb;

	/** Indicates whether this instance requires a commit of control stack
	 * data */
	bool commit;

	/** Indicates whether this instruction is injected by IDecode hence
	 * shouldn't be killed. */
	bool injected;

	/** Line of occurrence for this instruction. */
	int line;

	/** BB identifier for this instruction. */
	int bb;

	/** True iff this instruction ends with an unconditional exit. */
	bool post_exit;

	/** Optional metadata. */
	Metadata *md;

	/** Parse the sub-operation from an input string.
	 * @param line Input string. */
	void parseSubop(string &line);

	/** Parse an operand from an input string.
	 * @param s Input string
	 * @param oper Reference to an output operand object. */
	void parseOperand(string &s, Operand &oper);

	/** Check whether the operand type is permitted by the typemask.
	 * @param oper Operand to check.
	 * @param typemask Typemask to check the operand type against.
	 * @return True iff this operand is permitted by the typemask. */
	bool checkOperandType(Operand &oper, unsigned int typemask);

public:
	/** Construct an empty (invalid) instruction */
	Instruction();

	/** Shorthand useful for NOP */
	Instruction(ISAOp operation);
	/** Construct an instruction with no operands */
	Instruction(ISAOp operation, ISASubOp suboperation);
	/** Construct an instruction with 1 destination, 0 source operands */
	Instruction(ISAOp operation, ISASubOp suboperation,
			Operand destination);
	/** Construct an instruction with 1 destination, 1 source operand */
	Instruction(ISAOp operation, ISASubOp suboperation, Operand destination,
			Operand source0);
	/** Construct an instruction with 1 destination, 2 source operands */
	Instruction(ISAOp operation, ISASubOp suboperation, Operand destination,
			Operand source0, Operand source1);
	/** Construct an instruction with 1 destination, 3 source operands */
	Instruction(ISAOp operation, ISASubOp suboperation, Operand destination,
			Operand source0, Operand source1, Operand source2);

	/** Construct an instruction from an input string.
	 * @param op Input string containing the operation name.
	 * @param l Input string containing the full line minus the op.
	 * @param ln Line number for this operation, tracked for debugging
	 * 	     purposes. */
	Instruction(string &op, string &l, int ln);

	/** Destructor */
	~Instruction(void);

	/** Validate whether instruction is valid or not.
	 * @return true iff this object is valid. */
	bool validate(void);

	/** Get number of source operands.
	 * @return The number of source operands set for this instruction. */
	unsigned int getSrcs(void);

	/** Get source operand.
	 * @param s Source operand number
	 * @return The source operand */
	Operand &getSrc(unsigned int s);

	/** Retrieve operation
	 * @return Instruction type, element from the ISAOp enumeration. */
	ISAOp getOp() const;

	/** Retrieve suboperation
	 * @return Suboperation union. */
	ISASubOp getSubOp(void) const;

	/** Retrieve the commit bit.
	 * @return True iff this instruction is a stack instruction whose data
	 *	   and/or jump target must be commited. */
	bool getCommit(void) const;

	/** Set the commit bit. */
	void setCommit(bool);

	/**
	 * Return true iff this instruction is a vector instruction.
	 * @return true iff this instruction is a vector instruction.
	 */
	bool isVectorInstruction(void);

	/** Return true iff this instruction is uninterruptible.
	 *
	 * True for all instructions that write to the control mask.
	 * @return true iff this instruction is uninterruptible. */
	bool isUninterruptible(void) const;

	/** Return true iff this instruction must block on SSP writes in the
	 * pipeline.
	 *
	 * True for some load/store instructions that take arguments from SSP
	 * registers.
	 * @return true iff this instruction must block on SSP writes. */
	bool blockOnSSPWrites(void) const;

	/** Return true iff this instruction performs a CPUSH.
	 * @return true iff this instruction performs a CPUSH. */
	bool doesCPUSH(void) const;

	/** Return true iff this instruction has a destination operand.
	 * @return true iff this instruction has a destination operand. */
	bool hasDst(void) const;

	/** Get destination operand.
	 * @return Destination operand, throws exception if non-existent. */
	Operand getDst(void) const;

	/** Return true iff this operation writes to a control (predicate) mask.
	 * @return True iff this operation writes to a CMASK. */
	bool writesCMASK(void) const;

	/** Add a source operand.
	 * @param op Operand to add to this instruction. */
	void addSrc(Operand op);

	/** Overwrite destination operand.
	 * @param op Operand to set destination to. */
	void setDst(Operand op);

	/** Kill instruction in the pipeline (as result of e.g. a branch).
	 *
	 * Prevents execution later in the pipeline, but permits operands to
	 * continue to flow through for scoreboard tracking. */
	void kill(void);

	/** Query whether this instance of the instruction is dead.
	 * @return true iff this instruction was killed in the pipeline. */
	bool isDead(void) const;

	/** Mark that this instruction has been enqueued on the scoreboard.
	 * @param sb True iff this intruction is enqueued on the scoreboard. */
	void setOnSb(bool sb);

	/** Has the destination operand been enqueued on the scoreboard?
	 * @return True iff a scoreboard entry for this ops destination operand
	 * 	   exists. */
	bool getOnSb(void) const;

	/** Mark that this instruction has been counted on the CStack
	 * scoreboard.
	 * @param sb True iff this instruction is counted on the CStack
	 * 	     scoreboard. */
	void setOnCStackSb(bool sb);

	/** Has the destination operand been enqueued on the cstack scoreboard?
	 * @return True iff a scoreboard entry for this operation's destination
	 * 	   operand exists. */
	bool getOnCStackSb(void) const;

	/** Mark this instruction as injected. Only for CPOP. */
	void inject(void);

	/** Return true iff this instruction is injected in the pipeline.
	 * @return True iff this instruction is injected. */
	bool isInjected(void);

	/** Mark this instruction as the last instruction of the kernel.
	 *
	 * Can only be done on DRAM write operations.
	 * @return True iff this instruction was successfully marked as
	 * 	   unconditionally exiting. */
	bool setExit(void);
	/** Return true iff this instruction marks the last instruction of the
	 * kernel
	 *
	 * @return True iff the kernel must uncoditionally exit after executing
	 * this instruction. */
	bool postExit(void);

	/** Return true iff this operation is a load/store operation.
	 * @return True iff operation is a load operation. */
	bool ldst(void) const;

	/** Return true iff this operation is a global (DRAM) store operation.
	 * @return true iff this operation is a global (DRAM) store operation.*/
	bool stg(void) const;

	/** Return true iff this operation loads/stores from scratchpad to
	 * the register file. */
	bool ldstsp(void) const;

	/** Return true iff this instruction terminates the BB.
	 * @return True iff this instruction terminates the BB. */
	bool bbFinish(void) const;

	/** If this instruction is a branch instruction, return branch taken
	 * destination.
	 * @return A pointer to the BB reached by the branch operation in this
	 * 	   instruction, or nullptr if no such BB exists. */
	BB *getBranchTakenDst(void) const;

	/** Returns true iff this instruction has a "branch not taken" path.
	 * This includes all non-ldst instructions
	 * @return True iff this instruction has a branch-not-taken path. */
	bool canBranchNotTaken(void) const;

	/** Returns true iff this instruction may take a branch.
	 *
	 * Takes into account branchcycle annotations and state.
	 * @return True iff this instruction may take the branch. */
	bool mayTakeBranch(void) const;

	/** Returns true iff this instruction may fall through to the next BB.
	 *
	 * Takes into account branchcycle annotations and state.
	 * @return True iff this instruction may fall through. */
	bool mayTakeFallthrough(void) const;

	/** Increments the current branch cycle state. */
	void incrementBranchCycle(void);

	/** Resets the branch cycle state to the initialisation value provided
	 * by the programmer annotation. */
	void resetBranchCycle(void);

	/** Determine the number of consecutive registers written by this
	 * instruction.
	 * @param sd_words Current value for sp.sd_words.
	 * @return The number of consecutive destination registers written. */
	unsigned int getConsecutiveDstRegs(unsigned int sd_words);

	/** Attach branchcycle or load/store cost metadata to this instruction.
	 * @param m Metadata to attach. */
	void addMetadata(Metadata *m);

	/** Return the branchcycle or load/store cost metadata associated with
	 * this instruction
	 * @return Pointer to the metadata associated with this instruction. */
	Metadata *getMetadata(void);

	/** Set the BB identifier of the BB containing this instruction.
	 *
	 * Tracked for debugging purposes.
	 * @param bbid The ID of the BB that contains this instruction. */
	void setBB(int bbid);

	/** Get the BB identifier of the BB containing this instruction.
	 *
	 * Tracked for debugging purposes.
	 * @return The ID of the BB that contains this instruction. */
	int getBB(void);

	/** Convert the operation and suboperation to a string.
	 * @return A string representation of the operation. */
	std::string opToString() const;

	/** SystemC mandatory print stream operation.
	 * @param os Output stream
	 * @param v The Instruction to print
	 * @return Output stream. */
	inline friend ostream &
	operator<<(ostream &os, Instruction const &v)
	{
		unsigned int i;

		os << "Instruction(";

		if (v.line >= 0)
			os << v.line << ": ";

		os << v.opToString();

		if (v.hasDst() || v.srcs > 0)
			os << " ";

		if (v.hasDst()) {
			os << v.dst;
			if (v.srcs > 0)
				os << ", ";
		}

		for (i = 0; i < v.srcs; i++) {
			if (i != 0)
				os << ", ";
			os << v.src[i];
		}

		os << ")";

		if (v.isDead())
			os << " dead";

		if (v.getCommit())
			os << " commit";

		if (v.on_cstack_sb)
			os << " on-CSTACK-SB";

		if (v.on_sb)
			os << " on-SB";

		if (v.injected)
			os << " injected";

		if (v.md)
			os << " " << *(v.md);

		return os;
	}

	/** SystemC mandatory trace function.
	 * @param tf Trace file pointer
	 * @param v Reference to instruction to trace.
	 * @param NAME Name of the signal. */
	inline friend void sc_trace(sc_trace_file *tf, const Instruction &v,
		const std::string &NAME ) {
	      sc_trace(tf,v.op, NAME + ".op");
	}

	/** Comparator.
	 * @param v Object to compare against.
	 * @return true iff v is equal to this object. */
	inline bool operator==(const Instruction & v) const {
		unsigned int i;

		if (op != v.op)
			return false;

		switch (op) {
		case OP_CPUSH:
			if (subop.cpush != v.subop.cpush)
				return false;
			break;
		case OP_TEST:
		case OP_ITEST:
		case OP_SICJ:
			if (subop.test != v.subop.test)
				return false;
			break;
		case OP_PBOOL:
			if (subop.pbool != v.subop.pbool)
				return false;
			break;
		case OP_CVT:
		case OP_SCVT:
			if (subop.cvt != v.subop.cvt)
				return false;
			break;
		case OP_LDGLIN:
		case OP_STGLIN:
		case OP_LDSPLIN:
		case OP_STSPLIN:
			if (subop.ldstlin != v.subop.ldstlin)
				return false;
			break;
		case OP_MAD:
		case OP_ADD:
		case OP_MUL:
			if (subop.fpumod != v.subop.fpumod)
				return false;
			break;
		case OP_BUFQUERY:
			if (subop.bufquery != v.subop.bufquery)
				return false;
			break;
		case OP_DBG_PRINTCMASK:
			if (subop.printcmask != v.subop.printcmask)
				return false;
			break;
		default:
			break;
		}

		if (srcs != v.srcs)
			return false;

		for (i = 0; i < v.srcs; i++) {
			if (src[i] != v.src[i])
				return false;
		}

		if (dst != v.dst || dead != v.dead || commit != v.commit ||
		    on_sb != v.on_sb || on_cstack_sb != v.on_cstack_sb ||
		    injected != v.injected || post_exit != v.post_exit)
			return false;

		return true;
	}

	/** Incomparator.
	 * @param v Object to compare against.
	 * @return true iff v is equal to this object. */
	inline bool operator!=(const Instruction & v) const {
		return !(*this == v);
	}

	/** Copy constructor.
	 * @param v Object to copy data from
	 * @return A reference to the newly created Instruction object,
	 * containing the same data as v. */
	inline Instruction&
	operator=(const Instruction &v)
	{
		op = v.op;
		switch (v.op) {
		case OP_CPUSH:
			subop.cpush = v.subop.cpush;
			break;
		case OP_TEST:
		case OP_ITEST:
		case OP_SICJ:
			subop.test = v.subop.test;
			break;
		case OP_PBOOL:
			subop.pbool = v.subop.pbool;
			break;
		case OP_CVT:
		case OP_SCVT:
			subop.cvt = v.subop.cvt;
			break;
		case OP_LDGLIN:
		case OP_STGLIN:
		case OP_LDSPLIN:
		case OP_STSPLIN:
			subop.ldstlin = v.subop.ldstlin;
			break;
		case OP_MAD:
		case OP_ADD:
		case OP_MUL:
			subop.fpumod = v.subop.fpumod;
			break;
		case OP_BUFQUERY:
			subop.bufquery = v.subop.bufquery;
			break;
		case OP_DBG_PRINTCMASK:
			subop.printcmask = v.subop.printcmask;
			break;
		default:
			break;
		}

		dst = v.dst;
		dead = v.dead;
		commit = v.commit;
		injected = v.injected;
		on_sb = v.on_sb;
		on_cstack_sb = v.on_cstack_sb;
		line = v.line;
		post_exit = v.post_exit;
		if (v.md)
			md = v.md->clone();
		else
			md = nullptr;

		srcs = v.srcs;
		for (unsigned int i = 0; i < v.srcs; i++)
			src[i] = v.src[i];

		bb = v.bb;

		return *this;
	}
};

/** Print the operation in LaTeX format.
 * @param op Operation to print
 * @param s Output stream. */
void printOp(ISAOp op, ostream *s);

/** Return the operation category for given operation.
 * @param op Operation code
 * @return Operation category for this operation. */
ISACategory opCategory(ISAOp op);

}

#endif /* COMPUTE_MODEL_ISA_INSTRUCTION_H */
