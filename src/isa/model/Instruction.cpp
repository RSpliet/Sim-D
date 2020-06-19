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

#include <array>
#include <utility>
#include <cassert>
#include <sstream>

#include "isa/model/Instruction.h"
#include "util/parse.h"

using namespace isa_model;
using namespace std;

const string isa_model::cat_str[CAT_SENTINEL] = {
	"Floating point arithmetic",
	"Reciprocal/Trigonometry (expensive FP arith)",
	"Integer/Boolean arithmetic",
	"Data copy, conversion and intra-lane shuffle",
	"Load/Store",
	"Control flow",
	"Predicate manipulation",
	"Debug"
};

#define OP_OMIT (1u << REGISTER_NONE)
#define OP_VGPR (1u << REGISTER_VGPR)
#define OP_SGPR (1u << REGISTER_SGPR)
#define OP_PR   (1u << REGISTER_PR)
#define OP_IMM  (1u << REGISTER_IMM)
#define OP_VSP  (1u << REGISTER_VSP)
#define OP_SSP  (1u << REGISTER_SSP)

static const pair<const string, const string> subop_test_str[TEST_SENTINEL] = {
	[TEST_EZ] = {"ez","Equal to Zero (0.f or -0.f)."},
	[TEST_NZ] = {"nz","Non-equal to Zero."},
	[TEST_G] = {"g","Greater than zero."},
	[TEST_GE] = {"ge","Greater than or Equal to zero."},
	[TEST_L] = {"l","Less than zero."},
	[TEST_LE] = {"le","Less than or equal to zero."}
};

static const pair<const string, const string> subop_itest_str[TEST_SENTINEL] = {
	[TEST_EZ] = {"ez","Equal to Zero."},
	[TEST_NZ] = {"nz","Non-equal to Zero."},
	[TEST_G] = {"g","Greater than zero."},
	[TEST_GE] = {"ge","Greater than or Equal to zero."},
	[TEST_L] = {"l","Less than zero."},
	[TEST_LE] = {"le","Less than or equal to zero."}
};

static const pair<const string, const string> subop_pbool_str[PBOOL_SENTINEL] = {
	{"and","Boolean AND."},
	{"or","Boolean OR."},
	{"nand","Boolean Not-AND"},
	{"nor","Boolean Not-OR"},
};

static const pair<const string, const string> subop_cpush_str[CPUSH_SENTINEL] = {
	{"if","Control mask."},
	{"brk","Break mask."},
	{"jc","Call/return mask."}
};

static const pair<const string, const string> subop_cvt_str[CVT_SENTINEL] = {
	{"i2f","Integer to Float."},
	{"f2i","Float to Integer."}
};

static const pair<const string, const string> subop_ldstlin_str[LIN_SENTINEL] = {
	{"","Unit mapped elements."},
	{"vec2","Vec2 elements to consecutive registers."},
	{"vec4","Vec4 elements to consecutive registers."}
};

static const pair<const string, const string> subop_printcmask_str[PRINTCMASK_SENTINEL] = {
	{"if","Control mask."},
	{"brk","Break mask."},
	{"jc","Call/return mask."},
	{"exit","Exit mask."},
};

static const pair<const string, const string> mod_fpu_str[FPU_SENTINEL] = {
	{"","Normal operation."},
	{"neg","Negate second operand."},
};

static const pair<const string, const string> subop_bufquery_str[BUFQUERY_SENTINEL] = {
	{"dim_x","Buffer width, in number of elements (32-bit words)."},
	{"dim_y","Buffer height."},
};

namespace isa_model {
/** Instruction specification.
 *
 * Used for validation, parsing and documentation. */
class ISAOpSpec
{
public:
	/** Instruction type. */
	ISACategory cat;

	/** Instruction name. */
	string name;

	/** Highest subop value. */
	unsigned int subops;

	/** String subop translation */
	const pair<const string, const string> *subop_str;

	/** Minimal number of source operands. */
	unsigned int srcs;
	/** Permitted source operand type mask. */
	unsigned int src_type[3];

	/** Permitted destination operand type mask (OP_OMIT -> implicit dst).*/
	unsigned int dst_type;

	/** True iff this is a vector instruction */
	bool vec;

	/** Must wait for special purpose register stores to finish. */
	bool block_ssp_writes;

	/** Performs a CPUSH. */
	bool cpush;

	/** One-liner description. */
	string description;

	/** Potential multi-line documentation. */
	string documentation;

	/** Default empty constructor. */
	ISAOpSpec()
	: cat(CAT_DEBUG), name("ERROR"), subops(0), subop_str(nullptr), srcs(0),
	  dst_type(OP_OMIT), vec(false), block_ssp_writes(false),
	  cpush(false), description(""), documentation("")
	{}

	/** Constructor for instruction with no source operands, no subops.
	 * @param c Instruction category
	 * @param n Name of this operation
	 * @param d Destination type
	 * @param v True iff this instruction is a vector instruction
	 * @param bssp True iff this instruction must block on pending SSP
	 * 	       writes.
	 * @param cp True iff this instruction performs a control stack push.
	 * @param desc One-line description, for generated LaTeX documentation.
	 * @param doc Full description, for generated LaTeX documentation. */
	ISAOpSpec(ISACategory c, const string n,
			unsigned int d, bool v, bool bssp, bool cp,
			string desc, string doc = "")
	: cat(c), name(n), subops(0), subop_str(nullptr), srcs(0), dst_type(d),
	  vec(v), block_ssp_writes(bssp), cpush(cp),
	  description(desc), documentation(doc)
	{}

	/** Constructor for instruction with one source operand, no subops.
	 * @param c Instruction category
	 * @param n Name of this operation
	 * @param src0 Type mask for first source operand.
	 * @param d Destination type mask
	 * @param v True iff this instruction is a vector instruction
	 * @param bssp True iff this instruction must block on pending SSP
	 * 	       writes.
	 * @param cp True iff this instruction performs a control stack push.
	 * @param desc One-line description, for generated LaTeX documentation.
	 * @param doc Full description, for generated LaTeX documentation. */
	ISAOpSpec(ISACategory c, const string n, unsigned int src0,
			unsigned int d, bool v, bool bssp, bool cp,
			string desc, string doc)
	: cat(c), name(n), subops(0), subop_str(nullptr), srcs(1), dst_type(d),
	  vec(v), block_ssp_writes(bssp), cpush(cp),
	  description(desc), documentation(doc)
	{
		src_type[0] = src0;
	}

	/** Constructor for instruction with two source operands, no subops.
	 * @param c Instruction category
	 * @param n Name of this operation
	 * @param src0 Type mask for first source operand.
	 * @param src1 Type mask for second source operand.
	 * @param d Destination type mask
	 * @param v True iff this instruction is a vector instruction
	 * @param bssp True iff this instruction must block on pending SSP
	 * 	       writes.
	 * @param cp True iff this instruction performs a control stack push.
	 * @param desc One-line description, for generated LaTeX documentation.
	 * @param doc Full description, for generated LaTeX documentation. */
	ISAOpSpec(ISACategory c, const string n, unsigned int src0,
			unsigned int src1, unsigned int d, bool v,
			bool bssp, bool cp, string desc, string doc)
	: cat(c), name(n), subops(0), subop_str(nullptr), srcs(2), dst_type(d),
	  vec(v), block_ssp_writes(bssp), cpush(cp),
	  description(desc), documentation(doc)
	{
		src_type[0] = src0;
		src_type[1] = src1;
	}

	/** Constructor for instruction with three source operands, no subops.
	 * @param c Instruction category
	 * @param n Name of this operation
	 * @param src0 Type mask for first source operand.
	 * @param src1 Type mask for second source operand.
	 * @param src2 Type mask for third source operand.
	 * @param d Destination type mask
	 * @param v True iff this instruction is a vector instruction
	 * @param bssp True iff this instruction must block on pending SSP
	 * 	       writes.
	 * @param cp True iff this instruction performs a control stack push.
	 * @param desc One-line description, for generated LaTeX documentation.
	 * @param doc Full description, for generated LaTeX documentation. */
	ISAOpSpec(ISACategory c, const string n, unsigned int src0,
			unsigned int src1, unsigned int src2,
			unsigned int d, bool v, bool bssp, bool cp,
			string desc, string doc)
	: cat(c), name(n), subops(0), subop_str(nullptr), srcs(3), dst_type(d),
	  vec(v), block_ssp_writes(bssp), cpush(cp),
	  description(desc), documentation(doc)
	{
		src_type[0] = src0;
		src_type[1] = src1;
		src_type[2] = src2;
	}

	/** Constructor for instruction with suboperations.
	 * @param c Instruction category
	 * @param n Name of this operation
	 * @param sub Number of possible sub-operation values.
	 * @param subs String-mapping of sub-operations.
	 * @param s Number of source operands
	 * @param src0 Type mask for first source operand.
	 * @param src1 Type mask for second source operand.
	 * @param src2 Type mask for third source operand.
	 * @param d Destination type mask
	 * @param v True iff this instruction is a vector instruction
	 * @param bssp True iff this instruction must block on pending SSP
	 * 	       writes.
	 * @param cp True iff this instruction performs a control stack push.
	 * @param desc One-line description, for generated LaTeX documentation.
	 * @param doc Full description, for generated LaTeX documentation. */
	ISAOpSpec(ISACategory c, const string n, unsigned int sub,
			const pair<const string, const string> *subs,
			unsigned int s, unsigned int src0, unsigned int src1,
			unsigned int src2, unsigned int d, bool v,
			bool bssp, bool cp, string desc, string doc)
	: cat(c), name(n), subops(sub), subop_str(subs), srcs(s), dst_type(d),
	  vec(v), block_ssp_writes(bssp), cpush(cp),
	  description(desc), documentation(doc)
	{
		src_type[0] = src0;
		src_type[1] = src1;
		src_type[2] = src2;
	}

private:
	/** Boolean used during LaTeX documentation generating indicating that
	 * the source operand currently being printed is the first. Used to get
	 * the commas right. */
	bool firstSrc;

	/** Print out text representations of an instruction.
	 *
	 * Recursive to deal with the combination of variable number of
	 * operands and optional operands.
	 * @param s Output stream to write to.
	 * @param pre Accumulator string containing operation plus all operands
	 *	      already processed.
	 * @param sno Source number to process this iteration.
	 * @param opt_bkts Accumulator counting number of "optional operand"
	 * 		   brackets.
	 * @param nodst Operation has no destination, omit printing first comma.
	 */
	void
	printSrcs(ostream *s, string pre, unsigned int sno,
			unsigned int opt_bkts, bool nodst = false)
	{
		string post;
		unsigned int i;
		unsigned int t;

		if (sno == srcs) {
			if (firstSrc)
				firstSrc = false;
			else
				*s << "\\\\";
			*s << pre;
			for (i = 0; i < opt_bkts; i++)
				*s << "]";
			return;
		}

		if (src_type[sno] & OP_OMIT) {
			opt_bkts++;
			pre = pre + "[";
		}

		for (t = REGISTER_SGPR; t < REGISTER_SENTINEL; t++) {
			if (!(src_type[sno] & (1 << t)))
				continue;

			/* Don't print VSP for for CALL and EXIT. These
			 * VSPs are only there for implicit-one and should not
			 * be written out in a program. */
			if (t == REGISTER_VSP &&
			    (src_type[sno] & (OP_OMIT | OP_VSP)) == (OP_OMIT | OP_VSP))
				continue;

			if (nodst)
				post = "";
			else
				post = ", ";

			switch (t) {
			case REGISTER_SGPR:
				post += "s" + to_string(sno);
				break;
			case REGISTER_VGPR:
				post += "v" + to_string(sno);
				break;
			case REGISTER_IMM:
				post += "imm" + to_string(sno);
				break;
			case REGISTER_PR:
				post += "p" + to_string(sno);
				break;
			case REGISTER_VSP:
				post += "vsp" + to_string(sno);
				break;
			case REGISTER_SSP:
				post += "ssp" + to_string(sno);
				break;
			default:
				post += "ERROR";
				break;
			}

			printSrcs(s, pre + post, sno+1, opt_bkts);
		}
	}

	/** LaTeX generated docs: print the table of sub-operations.
	 * @param s Output stream. */
	void
	printSubopTable(ostream *s)
	{
		unsigned i;

		*s << "\\begin{table}[H]" << endl;
		*s << "\\begin{tabular}{l|l}" << endl;
		*s << ".op & Description\\\\" << endl;
		*s << "\\hline" << endl;

		for (i = 0; i < subops; i++) {
			if (i)
				*s << "\\\\" << endl;

			if (subop_str[i].first == "")
				*s << "(omit)";
			else
				*s << escapeLaTeX(subop_str[i].first);

			*s << " & " <<	subop_str[i].second;
		}

		*s << "\\end{tabular}" << endl;
		*s << "\\end{table}" << endl;
	}

public:
	/** Print a LaTeX representation of this instruction.
	 * @param s Output stream to print to. */
	void
	toLaTeX(ostream *s)
	{
		unsigned int dt, i;

		string opname;
		string dst_str;
		bool printed;

		if (subops > 0) {
			if (getDefaultSubOp() == -1)
				opname = name + ".op ";
			else
				opname = name + "[.op] ";
		} else {
			opname = name + " ";
		}

		*s << "\\insn{" << name << "}{" << description << "}{";

		/* Print all permutations of parameters. */
		printed = false;
		firstSrc = true;
		if (dst_type & OP_OMIT) {
			printSrcs(s, opname, 0, 0, true);
		} else {
			for (dt = REGISTER_SGPR; dt < REGISTER_SENTINEL; dt++) {
				if (!(dst_type & (1 << dt)))
					continue;

				switch (dt) {
				case REGISTER_SGPR:
					dst_str = "sdst";
					break;
				case REGISTER_VGPR:
					dst_str = "vdst";
					break;
				case REGISTER_PR:
					dst_str = "pdst";
					break;
				case REGISTER_VSP:
					dst_str = "vsp";
					break;
				case REGISTER_SSP:
					dst_str = "ssp";
					break;
				case REGISTER_IMM:
					dst_str = "dimm";
					break;
				default:
					dst_str = "ERROR";
					break;
				}

				printSrcs(s, opname + dst_str, 0, 0);
			}
		}

		if (subops > 0) {
			*s << "\\\\[0.3cm]" << endl;
			*s << "op $\\in$ \\{";
			for (i = 0; i < subops; i++) {
				if (i > 0)
					*s << ",";
				*s << escapeLaTeX(subop_str[i].first);
			}
			*s << "\\}";
		}
		*s << "}" << endl;

		if (documentation != "")
			*s << "\\paragraph{Description} " << documentation <<
			endl << endl;

		if (subops)
			printSubopTable(s);
	}

	/** Retrieve the default sub-operation for this instruction.
	 * @return The default sub-operation for this instruction. */
	int
	getDefaultSubOp(void)
	{
		if (subops && subop_str[0].first == "")
			return 0;
		else
			return -1;
	}
};

}

ISAOpSpec op_validate[OP_SENTINEL] = {
	[NOP] = ISAOpSpec(CAT_ARITH_FP,"nop",OP_OMIT,false,false,false,"No operation",""),
	[OP_TEST] = ISAOpSpec(CAT_PREDICATE,"test",TEST_SENTINEL,subop_test_str,1,OP_VGPR,0,0,OP_PR,true,false,false,
			"Test floating point number against given condition.",
			"Tests each element in vector v0 against the condition "
			"provided in .op, produce 1 in the corresponding predicate "
			"register bit if the condition holds, 0 otherwise."),
	[OP_ITEST] = ISAOpSpec(CAT_PREDICATE,"itest",TEST_SENTINEL,subop_itest_str,1,OP_VGPR,0,0,OP_PR,true,false,false,
			"Test integer number against given condition.",
			"Tests each element in vector v0 against the condition "
			"provided in .op, produce 1 in the corresponding predicate "
			"register bit if the condition holds, 0 otherwise."),
	[OP_PBOOL] = ISAOpSpec(CAT_PREDICATE,"pbool",PBOOL_SENTINEL,subop_pbool_str,2,OP_PR,OP_PR,0,OP_PR,true,false,false,
			"Perform a boolean operation on two predicate registers.",
			"For each element n in the (vector) predicate register, "
			"perform pdst[n] = p0[n] (op) p1[n]."),
	[OP_J] = ISAOpSpec(CAT_CTRLFLOW,"j",OP_IMM,OP_OMIT,false,false,false,
			"Jump to an absolute location in the program.",
			"Update PC with the value given by imm0."),
	[OP_SICJ] = ISAOpSpec(CAT_CTRLFLOW,"sicj",TEST_SENTINEL,subop_itest_str,2,OP_IMM,OP_SGPR,0,OP_OMIT,false,false,false,
			"Scalar Integer Conditional Jump to an absolute location.",
			"If the integer in s1 passes the test specified by "
			"the suboperation, update PC with the value given by imm0."),
	[OP_BRA] = ISAOpSpec(CAT_CTRLFLOW,"bra",OP_IMM,OP_PR,OP_OMIT|OP_VSP,true,false,true,
			"Conditional (divergent) branch,",
			"Perform a branch conditional on p1 to a destination "
			"PC given in imm0."),
	[OP_CALL] = ISAOpSpec(CAT_CTRLFLOW,"call",OP_IMM,OP_PR|OP_VSP|OP_OMIT,OP_OMIT|OP_VSP,true,false,true,
			"Call",
			"Call a function at the PC given by imm0. Conditional "
			"on p1. Will push a call type entry onto the control "
			"stack for return purposes."),
	[OP_CPUSH] = ISAOpSpec(CAT_CTRLFLOW,"cpush",CPUSH_SENTINEL,subop_cpush_str,2,OP_IMM,OP_PR|OP_OMIT,0,OP_OMIT,true,false,true,
			"Push an element onto the control stack.",
			"Store a control flow entry onto the control stack. "
			"imm0 specifies the PC to push. p1 defines "
			"an optional predicate register to push. If p1 is "
			"omitted, the CMASK corresponding to the given "
			"suboperation will be loaded."),
	[OP_CMASK] = ISAOpSpec(CAT_CTRLFLOW,"cmask",OP_PR,OP_OMIT|OP_VSP,true,false,false,
			"Manipulate the ``control'' CMASK directly",
			"Disable all threads t for which p0[t] is set to 1. "
			"Used in part to implement C and C++'s ``continue'' "
			"statement to skip to the next iteration of a for-loop."),
	[OP_CPOP] = ISAOpSpec(CAT_CTRLFLOW,"cpop",OP_OMIT,true,false,false,
			"Pop an element off the control stack.","Pops an entry "
			"off the control stack, which is equivalent to either ending "
			"the innermost control flow action (such as brk or call) or, "
			"in the case of bra, to continue execution of the else branch."),
	[OP_RET] = ISAOpSpec(CAT_CTRLFLOW,"ret",OP_PR,OP_OMIT|OP_VSP,true,false,false,
			"Conditional return.",
			"Return from call conditional on predicate register p0. "
			"For unconditional return, use CPOP."),
	[OP_BRK] = ISAOpSpec(CAT_CTRLFLOW,"brk",OP_PR,OP_OMIT|OP_VSP,true,false,false,
			"Conditional break.",
			"Disable all threads t for which p0[t] is set to 1. Used "
			"in part to implement C and C++'s ``break'' statement "
			"to break out of a for-loop. For an unconditional "
			"break, use CPOP."),
	[OP_EXIT] = ISAOpSpec(CAT_CTRLFLOW,"exit",OP_PR|OP_VSP|OP_OMIT,OP_OMIT|OP_VSP,true,false,false,
			"Exit program,",
			"Exits program. Can optionally be conditional on predicate "
			"register p0."),
	[OP_MAD] = ISAOpSpec(CAT_ARITH_FP,"mad",FPU_SENTINEL,mod_fpu_str,3,OP_VGPR,OP_VGPR|OP_SGPR|OP_IMM,OP_VGPR,OP_VGPR,true,false,false,
			"Multiply-Accumulate",
			"For each vector element n, performs vdst[n] = v0[n] * "
			"v1[n] + v2[n]. Operand 1 may also be a scalar "
			"register or immediate"),
	[OP_MUL] = ISAOpSpec(CAT_ARITH_FP,"mul",FPU_SENTINEL,mod_fpu_str,2,OP_VGPR,OP_VGPR|OP_SGPR|OP_IMM,0,OP_VGPR,true,false,false,
			"Floating-point multiply",
			"For each vector element n, performs vdst[n] = v0[n] * "
			"v1[n]. Operand 1 may also be a scalar "
			"register or immediate."),
	[OP_ADD] = ISAOpSpec(CAT_ARITH_FP,"add",FPU_SENTINEL,mod_fpu_str,2,OP_VGPR,OP_VGPR|OP_SGPR|OP_IMM,0,OP_VGPR,true,false,false,
			"Floating-point addition",
			"For each vector element n, performs vdst[n] = v0[n] + "
			"v1[n]. Operand 1 may also be a scalar "
			"register or immediate."),
	[OP_MIN] = ISAOpSpec(CAT_ARITH_FP,"min",OP_VGPR,OP_VGPR|OP_SGPR|OP_IMM,OP_VGPR,true,false,false,
			"Floating-point min",
			"For each vector element n, performs vdst[n] = min(v0[n], "
			"v1[n]). Operand 1 may also be a scalar "
			"register or immediate."),
	[OP_MAX] = ISAOpSpec(CAT_ARITH_FP,"max",OP_VGPR,OP_VGPR|OP_SGPR|OP_IMM,OP_VGPR,true,false,false,
			"Floating-point max",
			"For each vector element n, performs vdst[n] = max(v0[n], "
			"v1[n]). Operand 1 may also be a scalar "
			"register or immediate."),
	[OP_ABS] = ISAOpSpec(CAT_ARITH_FP,"abs",OP_VGPR,OP_VGPR,true,false,false,
			"Floating-point absolute",
			"For each vector element n, performs vdst[n] = $\\vert$v0[n]$\\vert$."),
	[OP_MOV] = ISAOpSpec(CAT_DATA_COPY,"mov",OP_IMM|OP_VSP,OP_VGPR,true,false,false,
			"Move immediate or special register to vdst.",
			"Move an immediatevalue  or special purpose vector register into the "
			"lanes of vector register vdst."),
	[OP_MOVVSP] = ISAOpSpec(CAT_DATA_COPY,"movvsp",OP_IMM|OP_VGPR,OP_VSP,true,false,false,
			"Move immediate or vector register to vsp.",
			"Move an immediate or vector register into every "
			"lane of a special purpose vector register in vsp. Used "
			"primarily for cam-based indexed load/store."),
	[OP_SMOVSSP] = ISAOpSpec(CAT_DATA_COPY,"smovssp",OP_IMM|OP_SGPR,OP_SSP,false,false,false,
			"Move immediate or scalar register to ssp.",
			"Move an immediate or scalar register value into "
			"a special purpose scalar register ssp. Used "
			"primarily for setting custom stride descriptor parameters."),
	[OP_CVT] = ISAOpSpec(CAT_DATA_COPY,"cvt",CVT_SENTINEL,subop_cvt_str,1,OP_VSP|OP_SSP|OP_VGPR,0,0,OP_VGPR,true,false,false,
			"Convert vector between floating point and integer formats",
			"Moves a vector- or special purpose register into vector register "
			"vdst, converting between float and integer."),
	[OP_SCVT] = ISAOpSpec(CAT_DATA_COPY,"scvt",CVT_SENTINEL,subop_cvt_str,1,OP_SGPR|OP_SSP,0,0,OP_SGPR,false,false,false,
			"Convert scalar between floating point and integer formats",
			"Moves a (special purpose) scalar register into scalar register "
			"sdst, converting between float and integer."),
	[OP_BUFQUERY] = ISAOpSpec(CAT_DATA_COPY,"bufquery",BUFQUERY_SENTINEL,subop_bufquery_str,1,OP_IMM,0,0,OP_SGPR,false,false,false,
			"Query global buffer properties.",
			"Queries the property of a mapped buffer defined in .op."),
	[OP_IADD] = ISAOpSpec(CAT_ARITH_INT,"iadd",OP_VGPR,OP_VGPR|OP_SGPR|OP_IMM,OP_VGPR,true,false,false,
			"(Signed) integer addition",
			"For each vector element n, performs vdst[n] = v0[n] + "
			"v1[n]. Operand 1 may also be a scalar "
			"register or immediate."),
	[OP_ISUB] = ISAOpSpec(CAT_ARITH_INT,"isub",OP_VGPR,OP_VGPR|OP_SGPR|OP_IMM,OP_VGPR,true,false,false,
			"Signed integer subtraction",
			"For each vector element n, performs vdst[n] = v0[n] - "
			"v1[n]. Operand 1 may also be a scalar "
			"register or immediate."),
	[OP_IMUL] = ISAOpSpec(CAT_ARITH_INT,"imul",OP_VGPR,OP_VGPR|OP_SGPR|OP_IMM,OP_VGPR,true,false,false,
			"Signed integer multiply",
			"For each vector element n, performs vdst[n] = v0[n] * "
			"v1[n]. Operand 1 may also be a scalar "
			"register or immediate."),
	[OP_IMAD] = ISAOpSpec(CAT_ARITH_INT,"imad",OP_VGPR,OP_VGPR|OP_SGPR|OP_IMM,OP_VGPR,OP_VGPR,true,false,false,
			"Signed integer Multiply-Accumulate",
			"For each vector element n, performs vdst[n] = v0[n] * "
			"v1[n] + v2[n]. Operand 1 may also be a scalar "
			"register or immediate."),
	[OP_IMIN] = ISAOpSpec(CAT_ARITH_INT,"imin",OP_VGPR,OP_VGPR|OP_SGPR|OP_IMM,OP_VGPR,true,false,false,
			"Signed integer min",
			"For each vector element n, performs vdst[n] = min(v0[n], "
			"v1[n]). Operand 1 may also be a scalar "
			"register or immediate."),
	[OP_IMAX] = ISAOpSpec(CAT_ARITH_INT,"imax",OP_VGPR,OP_VGPR|OP_SGPR|OP_IMM,OP_VGPR,true,false,false,
			"Signed integer max",
			"For each vector element n, performs vdst[n] = max(v0[n], "
			"v1[n]). Operand 1 may also be a scalar "
			"register or immediate."),
	[OP_SHL] = ISAOpSpec(CAT_ARITH_INT,"shl",OP_VGPR,OP_SGPR|OP_IMM,OP_VGPR,true,false,false,
			"Left shift.",
			"Shift each value v0[n] left by s1/imm1 bits, store the result in vdst."),
	[OP_SHR] = ISAOpSpec(CAT_ARITH_INT,"shr",OP_VGPR,OP_SGPR|OP_IMM,OP_VGPR,true,false,false,
			"Right shift.",
			"Shift each value v0[n] right by s1/imm1 bits, store the result in vdst."),
	[OP_AND] = ISAOpSpec(CAT_ARITH_INT,"and",OP_VGPR,OP_VGPR|OP_SGPR|OP_IMM,OP_VGPR,true,false,false,
			"Boolean AND",
			"For each vector element n, performs vdst[n] = v0[n] \\& "
			"v1[n]. Operand 1 may also be a scalar "
			"register or immediate."),
	[OP_OR] = ISAOpSpec(CAT_ARITH_INT,"or",OP_VGPR,OP_VGPR|OP_SGPR|OP_IMM,OP_VGPR,true,false,false,
			"Boolean OR",
			"For each vector element n, performs vdst[n] = v0[n] $\\vert$ "
			"v1[n]. Operand 1 may also be a scalar "
			"register or immediate."),
	[OP_XOR] = ISAOpSpec(CAT_ARITH_INT,"xor",OP_VGPR,OP_VGPR|OP_SGPR|OP_IMM,OP_VGPR,true,false,false,
			"Boolean XOR",
			"For each vector element n, performs vdst[n] = v0[n] $\\oplus$ "
			"v1[n]. Operand 1 may also be a scalar "
			"register or immediate."),
	[OP_NOT] = ISAOpSpec(CAT_ARITH_INT,"not",OP_VGPR,OP_VGPR,true,false,false,
			"Boolean NOT",
			"For each vector element n, performs vdst[n] = $\\sim$v0[n]."),
	[OP_SMOV] = ISAOpSpec(CAT_DATA_COPY,"smov",OP_SSP|OP_IMM,OP_SGPR,false,false,false,
			"Load scalar special register into an SGPR.",
			"Load scalar special register into an SGPR."),
	[OP_SIADD] = ISAOpSpec(CAT_ARITH_INT,"siadd",OP_SGPR,OP_SGPR|OP_IMM,OP_SGPR,false,false,false,
			"Scalar integer addition.",
			"Add the value of the two scalar integer operands, store in sdst."),
	[OP_SISUB] = ISAOpSpec(CAT_ARITH_INT,"sisub",OP_SGPR,OP_SGPR|OP_IMM,OP_SGPR,false,false,false,
			"Scalar integer subtraction.",
			"Subtract the value of the two scalar integer operands, store in sdst."),
	[OP_SIMUL] = ISAOpSpec(CAT_ARITH_INT,"simul",OP_SGPR,OP_SGPR|OP_IMM,OP_SGPR,false,false,false,
			"Scalar integer multiplication.",
			"Multiply the value of the two scalar integer operands, store in sdst."),
	[OP_SIMAD] = ISAOpSpec(CAT_ARITH_INT,"simad",OP_SGPR,OP_SGPR|OP_IMM,OP_SGPR,OP_SGPR,false,false,false,
			"Scalar integer multiply-addition.",
			"Multiply the value of the two integer scalar operands, add the third, store in sdst."),
	[OP_SIMIN] = ISAOpSpec(CAT_ARITH_INT,"simin",OP_SGPR,OP_SGPR|OP_IMM,OP_SGPR,false,false,false,
			"Scalar signed integer min",
			"Performs sdst = min(s0, s1). Operand 1 may also be an "
			"immediate."),
	[OP_SIMAX] = ISAOpSpec(CAT_ARITH_INT,"simax",OP_SGPR,OP_SGPR|OP_IMM,OP_SGPR,false,false,false,
			"Scalar signed integer max",
			"Performs sdst = max(s0, s1). Operand 1 may also be an "
			"immediate."),
	[OP_SINEG] = ISAOpSpec(CAT_ARITH_INT,"sineg",OP_SGPR,OP_SGPR,false,false,false,
			"Scalar signed integer negate",
			"Performs sdst = -s0."),
	[OP_SIBFIND] = ISAOpSpec(CAT_ARITH_INT,"sibfind",OP_SGPR,OP_SGPR,false,false,false,
			"Find first non-sign bit in a scalar integer register.",
			"Return the index of the most significant non-sign bit "
			"in s0, or $\\sim$0 if no bit is found. Resembles a round-down log2(s0) on "
			"any positive integer s0."),
	[OP_SSHL] = ISAOpSpec(CAT_ARITH_INT,"sshl",OP_SGPR,OP_SGPR|OP_IMM,OP_SGPR,false,false,false,
			"Scalar left shift.",
			"Shift the value of s0 left by s1/imm1 bits, store the result in sdst."),
	[OP_SSHR] = ISAOpSpec(CAT_ARITH_INT,"sshr",OP_SGPR,OP_SGPR|OP_IMM,OP_SGPR,false,false,false,
			"Scalar right shift.",
			"Shift the value of s0 right by s1/imm1 bits, store the result in sdst."),
	[OP_SIDIV] = ISAOpSpec(CAT_ARITH_INT,"sidiv",OP_SGPR,OP_SGPR|OP_IMM,OP_SGPR,false,false,false,
			"Scalar integer division.",
			"Divide integer s0 by s1 or imm1, store in sdst."),
	[OP_SIMOD] = ISAOpSpec(CAT_ARITH_INT,"simod",OP_SGPR,OP_SGPR|OP_IMM,OP_SGPR,false,false,false,
			"Scalar integer modulo.",
			"Divide integer s0 by s1 or imm1, store modulo in sdst."),
	[OP_SAND] = ISAOpSpec(CAT_ARITH_INT,"sand",OP_SGPR,OP_SGPR|OP_IMM,OP_SGPR,false,false,false,
			"Scalar boolean AND.",
			"Performs sdst = s0 \\& s1 resp. sdst = s0 \\& imm1."),
	[OP_SOR] = ISAOpSpec(CAT_ARITH_INT,"sor",OP_SGPR,OP_SGPR|OP_IMM,OP_SGPR,false,false,false,
			"Scalar boolean OR.",
			"Performs sdst = s0 $\\vert$ s1 resp. sdst = s0 $\\vert$ imm1."),
	[OP_SNOT] = ISAOpSpec(CAT_ARITH_INT,"snot",OP_SGPR,OP_SGPR,false,false,false,
			"Scalar boolean NOT.",
			"Performs sdst = $\\sim$s0."),
	[OP_RCP] = ISAOpSpec(CAT_ARITH_RCPU,"rcp",OP_VGPR,OP_VGPR,true,false,false,
			"Floating-point reciprocal",
			"For each vector element n, performs vdst[n] = 1 / v0[n]"),
	[OP_RSQRT] = ISAOpSpec(CAT_ARITH_RCPU,"rsqrt",OP_VGPR,OP_VGPR,true,false,false,
			"Floating-point reciprocal square root",
			"For each vector element n, performs vdst[n] = 1 / sqrt(v0[n])"),
	[OP_SIN] = ISAOpSpec(CAT_ARITH_RCPU,"sin",OP_VGPR,OP_VGPR,true,false,false,
			"Floating-point sine",
			"For each vector element n, performs vdst[n] = sin(v0[n])"),
	[OP_COS] = ISAOpSpec(CAT_ARITH_RCPU,"cos",OP_VGPR,OP_VGPR,true,false,false,
			"Floating-point cosine",
			"For each vector element n, performs vdst[n] = cos(v0[n])"),
	[OP_LDGLIN] = ISAOpSpec(CAT_LDST,"ldglin",LIN_SENTINEL,subop_ldstlin_str,3,OP_IMM,OP_IMM|OP_SGPR|OP_OMIT,OP_IMM|OP_SGPR|OP_OMIT,OP_VGPR|OP_VSP,false,false,false,
			"Load from global buffer linear to thread configuration.",
			"This operation will load one word for each thread from "
			"the buffer specified in imm0, the offset "
			"for which is primarily determined by the thread configuration. "
			"Optionally offset by the x and y coordinates provided in imm1 and imm2. "
			"A destination of vc.mem\\_data will trigger an ``indexed'' load, "
			"where the indexes are taken from vc.mem\\_idx."),
	[OP_STGLIN] = ISAOpSpec(CAT_LDST,"stglin",LIN_SENTINEL,subop_ldstlin_str,3,OP_IMM,OP_IMM|OP_SGPR|OP_OMIT,OP_IMM|OP_SGPR|OP_OMIT,OP_VGPR|OP_VSP,false,false,false,
			"Store global linear",
			"This operation will store one word for each thread to "
			"the global (DRAM) buffer specified in imm0, the offset "
			"for which is primarily determined by the thread configuration. "
			"Optionally offset by the x and y coordinates provided in imm1 and imm2. "
			"A destination of vc.mem\\_data will trigger an ``indexed'' store, "
			"where the indexes are taken from vc.mem\\_idx."),
	[OP_LDGBIDX] = ISAOpSpec(CAT_LDST,"ldgbidx",OP_IMM,OP_VSP|OP_OMIT,false,false,false,
			"LOad whole Buffer to CAM-based InDeX registers.",
			"This operation launches an indexed load, streaming the "
			"entire buffer through the CAMs shared bus."),
	[OP_STGBIDX] = ISAOpSpec(CAT_LDST,"stgbidx",OP_IMM,OP_VSP|OP_OMIT,false,false,false,
			"STore whole Buffer to CAM-based index registers.",
			"This operation launches an indexed store, streaming the "
			"entire buffer through the CAMs shared bus."),
	[OP_LDGCIDX] = ISAOpSpec(CAT_LDST,"ldgcidx",OP_IMM,OP_IMM|OP_SGPR|OP_OMIT,OP_IMM|OP_SGPR|OP_OMIT,OP_VSP|OP_OMIT,false,true,false,
			"LOad Custom stride descriptor to CAM-based InDeX registers.",
			"This operation launches an indexed load with a custom stride "
			"descriptor for which words, periods and period\\_count are "
			"taken from the special-purpose scalar registers. s1/imm1 and"
			"s2/imm2 respectively describe the x- and y-offsets into the buffer."),
	[OP_STGCIDX] = ISAOpSpec(CAT_LDST,"stgcidx",OP_IMM,OP_IMM|OP_SGPR|OP_OMIT,OP_IMM|OP_SGPR|OP_OMIT,OP_VSP|OP_OMIT,false,true,false,
			"Store Custom Stride Descriptor to CAM-based index registers.",
			"This operation launches an indexed store with a custom stride "
			"descriptor for which words, periods and period\\_count are "
			"taken from the special-purpose scalar registers. s1/imm1 and"
			"s2/imm2 respectively describe the x- and y-offsets into the buffer."),
	[OP_LDGIDXIT] = ISAOpSpec(CAT_LDST,"ldgidxit",OP_IMM,OP_VGPR,false,false,false,
			"LOad from DRAM to CAMs, iterating over indexes.",
			"This operation launches an indexed load, iterating over "
			"indexes one by one."),
	[OP_STGIDXIT] = ISAOpSpec(CAT_LDST,"stgidxit",OP_IMM,OP_VGPR,false,false,false,
			"Store Custom Stride Descriptor to CAM-based index registers.",
			"This operation launches an indexed store, iterating over "
			"indexes one by one."),
	[OP_LDG2SPTILE] = ISAOpSpec(CAT_LDST,"ldg2sptile",OP_IMM,OP_IMM|OP_SGPR|OP_OMIT,OP_IMM|OP_SGPR|OP_OMIT,OP_IMM,false,false,false,
			"Load tile from DRAM buffer imm0 to scratchpad buffer dimm.",
			"This operation will load a tile of data from a DRAM buffer imm0 "
			"to scratchpad buffer dimm. Size is determined by the scratchpad "
			"buffer size."),
	[OP_STG2SPTILE] = ISAOpSpec(CAT_LDST,"stg2sptile",OP_IMM,OP_IMM|OP_SGPR|OP_OMIT,OP_IMM|OP_SGPR|OP_OMIT,OP_IMM,false,false,false,
			"Store tile to DRAM buffer imm0 from scratchpad buffer dimm.",
			"This operation will store a tile of data from scratchpad buffer dimm "
			"to DRAM buffer imm0. Size is determined by the scratchpad "
			"buffer size."),
	[OP_SLDG] = ISAOpSpec(CAT_LDST,"sldg",OP_IMM, OP_IMM|OP_OMIT,OP_SGPR,false,false,false,
			"Scalar load",
			"Load one or more words from DRAM buffer imm0 to sdst and "
			"subsequent scalar registers. imm1 specifies the number "
			"of words to be loaded, defaults to 1."),
	[OP_SLDSP] = ISAOpSpec(CAT_LDST,"sldsp",OP_IMM,OP_IMM|OP_SGPR|OP_OMIT,OP_IMM|OP_SGPR|OP_OMIT,OP_SGPR,false,true,false,
			"Load scalar from scratchpad",
			"Load one or more words from scratchpad buffer imm0 into "
			"sdst and subsequent scalar registers. imm1/s1 determines "
			"the x-offset, imm2/s2 the y-offset. The number of words "
			"loaded is controlled by sc.sd\\_words."),
	[OP_LDSPLIN] = ISAOpSpec(CAT_LDST,"ldsplin",OP_IMM,OP_IMM|OP_SGPR|OP_OMIT,OP_IMM|OP_SGPR|OP_OMIT,OP_VGPR|OP_VSP,false,false,false,
			"Load from scratchpad buffer linear to thread configuration.",
			"This operation will load one word for each thread from "
			"the scratchpad buffer specified in imm0, the offset "
			"for which is primarily determined by the thread configuration. "
			"Optionally offset by the x and y coordinates provided in imm1 and imm2. "
			"A destination of vc.mem\\_data will trigger an ``indexed'' load, "
			"where the indexes are taken from vc.mem\\_idx."),
	[OP_STSPLIN] = ISAOpSpec(CAT_LDST,"stsplin",OP_IMM,OP_IMM|OP_OMIT,OP_IMM|OP_SGPR|OP_OMIT,OP_VGPR|OP_VSP,false,false,false,
			"Store to scratchpad buffer from linear",
			"This operation will store one word for each thread to "
			"the scratchpad buffer specified in imm0, the offset "
			"for which is primarily determined by the thread configuration. "
			"Optionally offset by the x and y coordinates provided in imm1 and imm2. "
			"A destination of vc.mem\\_data will trigger an ``indexed'' store, "
			"where the indexes are taken from vc.mem\\_idx."),
	[OP_LDSPBIDX] = ISAOpSpec(CAT_LDST,"ldspbidx",OP_IMM,OP_VSP|OP_OMIT,false,false,false,
			"LOad whole ScratchPad Buffer to CAM-based InDeX registers.",
			"This operation launches an indexed load, streaming the "
			"entire buffer specified by imm0 through the CAMs shared bus."),
	[OP_STSPBIDX] = ISAOpSpec(CAT_LDST,"stspbidx",OP_IMM,OP_VSP|OP_OMIT,false,false,false,
			"STore whole ScratchPad Buffer to CAM-based index registers.",
			"This operation launches an indexed store, streaming the "
			"entire buffer specified by imm0 through the CAMs shared bus."),
	[OP_DBG_PRINTSGPR] = ISAOpSpec(CAT_DEBUG,"printsgpr",OP_SGPR,OP_OMIT,false,false,false,
			"Print the value of a scalar register",""),
	[OP_DBG_PRINTVGPR] = ISAOpSpec(CAT_DEBUG,"printvgpr",OP_VGPR,OP_IMM,OP_OMIT,false,false,false,
			"Print the value of a vector register lane",
			"imm1 specifies the lane number to print."),
	[OP_DBG_PRINTPR] = ISAOpSpec(CAT_DEBUG,"printpr",OP_PR,OP_OMIT,true,false,false,
			"Print the values of a predicate register",""),
	[OP_DBG_PRINTCMASK] = ISAOpSpec(CAT_DEBUG,"printcmask",PRINTCMASK_SENTINEL,subop_printcmask_str,0,0,0,0,OP_OMIT,true,false,false,
			"Print the value of a CMASK.",
			""),
	[OP_DBG_PRINTTRACE] = ISAOpSpec(CAT_DEBUG,"printtrace",OP_IMM,OP_OMIT,false,false,false,
			"Enable/disable trace printing in the simulator.",
			""),
};

Instruction::Instruction()
 : op(OP_SENTINEL), dst(Operand()), srcs(0), dead(true), on_sb(false),
   on_cstack_sb(false), commit(false), injected(false), line(-1),
   bb(-1), post_exit(false), md(nullptr)
{}

Instruction::Instruction(ISAOp operation)
 : op(operation), dst(Operand()), srcs(0), dead(false), on_sb(false),
   on_cstack_sb(false), commit(false), injected(false), line(-1),
   bb(-1), post_exit(false), md(nullptr)
{
	assert(validate());
}

Instruction::Instruction(ISAOp operation, ISASubOp suboperation)
 : op(operation), subop(suboperation), dst(Operand()), srcs(0), dead(false),
   on_sb(false), on_cstack_sb(false), commit(false), injected(false), line(-1),
   bb(-1), post_exit(false), md(nullptr)
{
	assert(validate());
}

Instruction::Instruction(ISAOp operation, ISASubOp suboperation,
		Operand destination)
 : op(operation), subop(suboperation), dst(destination), srcs(0), dead(false),
   on_sb(false), on_cstack_sb(false), commit(false), injected(false), line(-1),
   bb(-1), post_exit(false), md(nullptr)
{
	assert(validate());
}

Instruction::Instruction(ISAOp operation, ISASubOp suboperation,
		Operand destination, Operand source0)
 : op(operation), subop(suboperation), dst(destination), srcs(1), dead(false),
   on_sb(false), on_cstack_sb(false), commit(false), injected(false), line(-1),
   bb(-1), post_exit(false), md(nullptr)
{
	src[0] = source0;
	assert(validate());
}

Instruction::Instruction(ISAOp operation, ISASubOp suboperation,
		Operand destination, Operand source0, Operand source1)
 : op(operation), subop(suboperation), dst(destination), srcs(2), dead(false),
   on_sb(false), on_cstack_sb(false), commit(false), injected(false), line(-1),
   bb(-1), post_exit(false), md(nullptr)
{
	src[0] = source0;
	src[1] = source1;
	assert(validate());
}

Instruction::Instruction(ISAOp operation, ISASubOp suboperation,
		Operand destination, Operand source0, Operand source1,
		Operand source2)
 : op(operation), subop(suboperation), dst(destination), srcs(3), dead(false),
   on_sb(false), on_cstack_sb(false), commit(false), injected(false), line(-1),
   bb(-1), post_exit(false), md(nullptr)
{
	src[0] = source0;
	src[1] = source1;
	src[2] = source2;
	assert(validate());
}

Instruction::~Instruction(void)
{
	if (md)
		delete md;
}

bool
Instruction::checkOperandType(Operand &oper, unsigned int typemask)
{
	switch (oper.getType()) {
	case OPERAND_BRANCH_TARGET:
	case OPERAND_IMM:
		if (typemask & OP_IMM)
			return true;
		break;
	case OPERAND_REG:
		if (typemask & (1 << oper.getRegisterType()))
			return true;
		break;
	default:
		break;
	}

	return false;

}

Instruction::Instruction(string &op_s, string &l, int ln)
 : dead(false), on_sb(false), on_cstack_sb(false), commit(false),
   injected(false), line(ln), bb(-1), post_exit(false), md(nullptr)
{
	unsigned int i;
	Operand oper;
	string ins;

	srcs = 0;

	op = OP_SENTINEL;
	/* Find opcode with op */
	for (i = 0; i < OP_SENTINEL; i++) {
		if (op_validate[i].name == op_s) {
			op = (ISAOp) i;
			break;
		}
	}

	if (op == OP_SENTINEL)
		throw invalid_argument("Unknown operation \"" + op_s + "\"");

	parseSubop(l);

	/* Omitting a destination means it must be omitted. */
	if (!(op_validate[op].dst_type & OP_OMIT)) {
		parseOperand(l, dst);
		if (!checkOperandType(dst, op_validate[op].dst_type))
			throw invalid_argument("Destination operand of invalid "
					"type");
	}

	/* Source operands may be omitted if the omit bit is set. */
	for (i = 0; i < op_validate[op].srcs; i++) {
		parseOperand(l, oper);

		if (oper.getType() == OPERAND_NONE)
			break;

		do {
			if (checkOperandType(oper, op_validate[op].src_type[i])) {
				src[i] = oper;
				srcs = i + 1;
				break;
			}

			if (!(op_validate[op].src_type[i] & OP_OMIT))
				throw invalid_argument("Operand for source " +
						to_string(i) + "invalid");

			src[i] = Operand();
			i++;
		} while (i < op_validate[op].srcs);
	}

	try {
		validate();
	} catch (invalid_argument &e) {
		ostringstream stream;
		stream << *this;
		throw invalid_argument(string(e.what()) + " for " + stream.str() + "\"");
	}
}

void
Instruction::parseOperand(string &s, Operand &oper)
{
	oper = Operand(s);
}

void
Instruction::parseSubop(string &s)
{
	unsigned int i;
	string subop_str;

	if (op_validate[op].subops == 0)
		return;

	skip_whitespace(s);

	/* Beware of the side-effects in read_char! First a dot, then the id */
	if (!read_char(s, '.')) {
		if (op_validate[op].getDefaultSubOp() == -1) {
			throw invalid_argument("Operation \"" +
				op_validate[op].name + "\" requires a subop, "
				"none given");
		} else {
			subop.raw = op_validate[op].getDefaultSubOp();
			return;
		}
	}

	if (!read_id(s, subop_str))
		throw invalid_argument("Operation \"" + op_validate[op].name +
				"\" requires a subop, none given");

	for (i = 0; i < op_validate[op].subops; i++) {
		if (subop_str == op_validate[op].subop_str[i].first) {
			subop.raw = i;
			return;
		}
	}

	throw invalid_argument("Subop \"" + subop_str + "\" for operation \"" +
			op_validate[op].name + "\" invalid");

}

unsigned int
Instruction::getSrcs()
{
	return srcs;
}

Operand &
Instruction::getSrc(unsigned int s)
{
	if (s >= srcs)
		throw invalid_argument("Index out of bounds.");

	return src[s];
}

ISAOp
Instruction::getOp(void) const
{
	return op;
}

ISASubOp
Instruction::getSubOp(void) const
{
	return subop;
}

bool
Instruction::hasDst(void) const
{
	return dst.getType() != OPERAND_NONE;
}

Operand
Instruction::getDst() const
{
	return dst;
}

bool
Instruction::writesCMASK() const
{
	if (op == OP_CPOP)
		return true;

	if (!hasDst())
		return false;

	return dst.modifiesCMASK();
}


void
Instruction::addSrc(Operand op)
{
	src[srcs++] = op;
}

void
Instruction::setDst(Operand op)
{
	dst = op;
	assert(validate());
}

void
Instruction::kill()
{
	if (!injected)
		dead = true;
}

bool
Instruction::isDead() const
{
	return dead;
}

bool
Instruction::getCommit(void) const
{
	return commit;
}

void
Instruction::setCommit(bool c)
{
	commit = c;
}

void
Instruction::setOnSb(bool sb)
{
	on_sb = sb;
}

bool
Instruction::getOnSb(void) const
{
	return on_sb;
}


void
Instruction::setOnCStackSb(bool sb)
{
	on_cstack_sb = sb;
}

bool
Instruction::getOnCStackSb(void) const
{
	return on_cstack_sb;
}

void
Instruction::inject(void)
{
	injected = true;
}

bool
Instruction::isInjected(void)
{

	return injected;
}

bool
Instruction::setExit(void)
{
	if (!stg()) {
		cerr << "Last instruction before unconditional exit is not "
			"a global store operation. Generally indicates a "
			"violation of access/execute scheduling. Not folding "
			"exit" << endl;
		return false;
	}

	post_exit = true;

	return true;
}

bool
Instruction::postExit(void)
{
	return post_exit;
}

bool
Instruction::ldst(void) const
{
	if (op >= OP_SENTINEL)
		return false;

	return op_validate[op].cat == CAT_LDST;
}

bool
Instruction::stg(void) const
{
	switch (op) {
	case OP_STG2SPTILE:
	case OP_STGBIDX:
	case OP_STGCIDX:
	case OP_STGLIN:
	case OP_STGIDXIT:
		return true;
	default:
		return false;
	}
}


bool
Instruction::ldstsp(void) const
{
	switch (op) {
	case OP_LDSPBIDX:
	case OP_LDSPLIN:
	case OP_STSPBIDX:
	case OP_STSPLIN:
	case OP_SLDSP:
		return true;
	default:
		return false;
	}
}

string
Instruction::opToString() const
{
	string out;
	if (op == OP_SENTINEL)
		return "ERROR";

	out = op_validate[op].name;

	if (op_validate[op].subops > 0 &&
			op_validate[op].getDefaultSubOp() != subop.raw) {
		out += "." + op_validate[op].subop_str[subop.raw].first;
	}

	if (post_exit)
		out += ".post_exit";

	return out;
}

bool
Instruction::validate(void)
{
	if (op >= OP_SENTINEL)
		throw invalid_argument("Opcode invalid");

	if (op_validate[op].subops > 0 &&
	    subop.raw > op_validate[op].subops)
		throw invalid_argument("Subop invalid");

	if (srcs > op_validate[op].srcs)
		throw invalid_argument("Too many source operands");

	/* Test type of given operands. */
	for (unsigned int i = 0; i < op_validate[op].srcs; i++) {
		if (i >= srcs && !(op_validate[op].src_type[i] & OP_OMIT))
			throw invalid_argument("Missing source operand "+
					to_string(i)+" for "+opToString());

		if (i < srcs &&
		    !(op_validate[op].src_type[i] & (1 << src[i].getRegisterType())))
			throw invalid_argument("Invalid type for source "
					"operand "+ to_string(i)+" for "+opToString());
	}

	if (!(op_validate[op].dst_type & (1u << dst.getRegisterType())))
		throw invalid_argument("Invalid destination operand");

	return true;
}

unsigned int
Instruction::getConsecutiveDstRegs(unsigned int sd_words)
{
	if (op >= OP_SENTINEL || !hasDst())
		return 0;

	switch (op) {
	case OP_SLDG:
		if (getSrcs() < 2) {
			return 1;
		} else {
			Operand &o = getSrc(1);
			assert(o.getType() == OPERAND_IMM);
			return o.getValue();
		}
		break;
	case OP_SLDSP:
		return sd_words;
		break;
	case OP_LDGLIN:
	case OP_STGLIN:
		return 1 << subop.raw;
		break;
	default:
		return 1;
	}
}

bool
Instruction::isVectorInstruction(void)
{
	if (op >= OP_SENTINEL)
		return false;

	return op_validate[op].vec;
}

bool
Instruction::blockOnSSPWrites(void) const
{
	if (op >= OP_SENTINEL)
		return false;

	return op_validate[op].block_ssp_writes;
}


bool
Instruction::doesCPUSH(void) const
{
	if (op >= OP_SENTINEL)
		return false;

	return op_validate[op].cpush;
}

bool
Instruction::bbFinish(void) const
{
	if (op >= OP_SENTINEL)
		return false;

	switch (op_validate[op].cat) {
	case CAT_LDST:
		return true;
		break;
	case CAT_CTRLFLOW:
		return op != OP_CPUSH;
		break;
	default:
		break;
	}

	return false;
}

BB *
Instruction::getBranchTakenDst(void) const
{
	if (op >= OP_SENTINEL || op_validate[op].cat != CAT_CTRLFLOW)
		return nullptr;

	switch (op)
	{
	case OP_J:
	case OP_SICJ:
	case OP_CALL:
		return src[0].getTargetBB();
	default:
		break;
	}

	return nullptr;
}

bool
Instruction::canBranchNotTaken(void) const
{
	if (op >= OP_SENTINEL || op_validate[op].cat != CAT_CTRLFLOW)
		return true;

	switch (op)
	{
	case OP_J:
	case OP_CPOP:
	case OP_CALL:
		return false;
	default:
		break;
	}

	return true;
}

bool
Instruction::mayTakeBranch(void) const
{
	if (op >= OP_SENTINEL || op_validate[op].cat != CAT_CTRLFLOW)
		return false;

	switch (op) {
	case OP_SICJ:
		if (!md)
			return true;

		return md->willBranch();
		break;
	case OP_CPUSH:
		return false;
	case OP_J:
	case OP_BRA:
	case OP_CPOP:
	case OP_RET:
	case OP_BRK:
	case OP_EXIT:
	case OP_CALL:
		/* Unconditional. */
		return true;
	default:
		throw invalid_argument("mayTakeBranch: Unknown op " +
				op_validate[op].name);
		break;
	}

	return false;
}

bool
Instruction::mayTakeFallthrough(void) const
{
	if (op >= OP_SENTINEL || op_validate[op].cat != CAT_CTRLFLOW)
		return true;

	switch (op) {
	case OP_SICJ:
		if (!md)
			return true;

		return !md->willBranch();
		break;
	case OP_J:
	case OP_CALL:
	case OP_CPOP:
		return false;
	case OP_BRA:
	case OP_CPUSH:
	case OP_CMASK:
	case OP_RET:
	case OP_BRK:
	case OP_EXIT:
		/* Unconditional. */
		return true;
	default:
		throw invalid_argument("mayTakeFallthrough: Unknown op " +
			op_validate[op].name);
		break;
	}

	return false;
}

void
Instruction::incrementBranchCycle(void)
{
	if (op >= OP_SENTINEL || op_validate[op].cat != CAT_CTRLFLOW || !md)
		return;

	md->incrementBranchCycle();
}

void
Instruction::resetBranchCycle(void)
{
	if (op >= OP_SENTINEL || op_validate[op].cat != CAT_CTRLFLOW || !md)
		return;

	md->resetBranchCycle();
}

void
Instruction::addMetadata(Metadata *m)
{
	md = m;
}

void
Instruction::setBB(int bbid)
{
	bb = bbid;
}

int
Instruction::getBB(void)
{
	return bb;
}

Metadata *
Instruction::getMetadata(void)
{
	return md;
}

void
isa_model::printOp(ISAOp op, ostream *s)
{
	if (op >= OP_SENTINEL)
		return;

	 op_validate[op].toLaTeX(s);
}

ISACategory
isa_model::opCategory(ISAOp op)
{
	if (op >= OP_SENTINEL)
		return CAT_SENTINEL;

	return op_validate[op].cat;
}

