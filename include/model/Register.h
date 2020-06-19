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

#ifndef MODEL_REGISTER_H
#define MODEL_REGISTER_H

#include <systemc>

#include "util/constmath.h"
#include "util/parse.h"

using namespace sc_dt;
using namespace sc_core;
using namespace std;

namespace simd_model {

typedef enum {
	REGISTER_NONE = 0,  /**< Empty operand. Used for instructions without
			 * conventional write-back, e.g. control flow. */
	REGISTER_SGPR = 1,  /**< Scalar general purpose register */
	REGISTER_VGPR,  /**< Vector general purpose register */
	REGISTER_PR,    /**< Predicate register */
	REGISTER_VSP,   /**< Special purpose registers. Some are read-only. */
	REGISTER_SSP,   /**< Scalar special purpose registers. */
	REGISTER_IMM,   /**< Immediate value */
	REGISTER_SENTINEL
} RegisterType;

/** (Special) register specification data type. */
typedef struct{
	const string alias; /**< Name/alias of this register. */
	const string doc;   /**< Documentation string for LaTeX docs/ */
	bool rw;	    /**< True iff this register is writable, false
	 	 	 	 iff read-only. */
} RegisterSpec;

/* Keep these VSP_CTRL mask indexes 0-3, used as index into register file */
#define VSP_CTRL_RUN   0
#define VSP_CTRL_BREAK 1
#define VSP_CTRL_RET   2
#define VSP_CTRL_EXIT  3
#define VSP_TID_X      4
#define VSP_TID_Y      5
#define VSP_LID_X      6
#define VSP_LID_Y      7
#define VSP_ZERO       8
#define VSP_ONE	       9
#define VSP_MEM_IDX   10
#define VSP_MEM_DATA  11
#define VSP_SENTINEL  12

static const RegisterSpec vsp_str[VSP_SENTINEL] = {
		[VSP_CTRL_RUN] = {"ctrl_run","Run control mask.",true},
		[VSP_CTRL_BREAK] = {"ctrl_break","Break control mask.",true},
		[VSP_CTRL_RET] = {"ctrl_ret","Return control mask.",true},
		[VSP_CTRL_EXIT] = {"ctrl_exit","Exit control mask.",true},
		[VSP_TID_X] = {"tid_x","Thread ID in X-dimension.",false},
		[VSP_TID_Y] = {"tid_y","Thread ID in Y-dimension.",false},
		[VSP_LID_X] = {"lid_x","Local thread ID (within work-group) in X-dimension.",false},
		[VSP_LID_Y] = {"lid_y","Local thread ID (within work-group) in X-dimension.",false},
		[VSP_ZERO] = {"zero","Hard-coded 0.",false},
		[VSP_ONE] = {"one","Hard-coded integer 1.",false},
		[VSP_MEM_IDX] = {"mem_idx","Indexes for CAM based memory r/w.",true},
		[VSP_MEM_DATA] = {"mem_data","Values to read/write for CAM based memory r/w",true},
};

#define SSP_DIM_X	  0
#define SSP_DIM_Y	  1
#define SSP_WG_OFF_X	  2
#define SSP_WG_OFF_Y	  3
#define SSP_WG_WIDTH      4
#define SSP_SD_WORDS      5
#define SSP_SD_PERIOD     6
#define SSP_SD_PERIOD_CNT 7
#define SSP_SENTINEL      8

static const RegisterSpec ssp_str[SSP_SENTINEL] = {
		[SSP_DIM_X] = {"dim_x","Kernel size (\\#threads) in X-dimension.",false},
		[SSP_DIM_Y] = {"dim_y","Kernel size (\\#threads) in Y-dimension.",false},
		[SSP_WG_OFF_X] = {"wg_off_x","Work-group offset within kernel invocation, TID\\_X of thread 0.",false},
		[SSP_WG_OFF_Y] = {"wg_off_y","Work-group offset within kernel invocation, TID\\_Y of thread 0.",false},
		[SSP_WG_WIDTH] = {"wg_width","Width of a workgroup as scheduled.",false},
		[SSP_SD_WORDS] = {"sd_words","Stride descriptor: Numer of words fetched in every period.",true},
		[SSP_SD_PERIOD] = {"sd_period","Stride descriptor: Numer of words in a period.",true},
		[SSP_SD_PERIOD_CNT] = {"sd_period_cnt","Stride descriptor: Numer of periods to repeat.",true}
};

/** Abstract register type
 *
 * @todo The name is no longer descriptive, as AbstractRegisters can be
 * instantiated. This is allowed to permit communication of AbstractRegisters
 * through SystemC ports, which doesn't pass around pointers.
 */
class AbstractRegister {
public:
	/** Type of operand to be written */
	RegisterType type;

	/** Which workgroup is active. */
	sc_uint<1> wg;

	/** Which row to write to. */
	sc_uint<const_log2(64)> row;

	/* Default empty constructor. */
	AbstractRegister();

	/** Constructor. */
	AbstractRegister(sc_uint<1> workgroup, RegisterType t,
			sc_uint<const_log2(64)> r);

	/** Virtual destructor. */
	virtual ~AbstractRegister(void);

	/** Clone method, allows copying with preservation of subtype info. */
	virtual AbstractRegister *clone(void) const;

	/** Return true iff this register is a Control Mask.
	 * @return True iff this register is a Control Mask.
	 */
	bool isCMASK() const;

	/** Returns true iff register type is a vector register type.
	 *
	 * Static to allow easy sharing with compute_model::Operator.
	 * @param rt Register type.
	 * @return True iff register type is a vector register type.
	 */
	static bool
	isVectorType(RegisterType rt)
	{
		switch (rt) {
		case REGISTER_VGPR:
		case REGISTER_PR:
		case REGISTER_VSP:
			return true;
		default:
			return false;
		}
	}

	/** Static print method
	 * Makes for easy sharing with subclasses and compute_model::Operator.
	 * @param os Output stream to print to
	 * @param rt Type of register.
	 * @param row Row of this register.
	 * @param latex True iff the output must have special characters escaped
	 * 		for LaTeX output.
	 */
	static ostream&
	print(ostream& os, RegisterType rt, unsigned int row, bool latex = false)
	{
		string s;

		switch (rt) {
		case REGISTER_SGPR:
			os << "s" << row;
			break;
		case REGISTER_VGPR:
			os << "v" << row;
			break;
		case REGISTER_PR:
			os << "p" << row;
			break;
		case REGISTER_VSP:
			if (latex)
				s = escapeLaTeX(vsp_str[row].alias);
			else
				s = vsp_str[row].alias;

			os << "vc." << s;
			break;
		case REGISTER_SSP:
			if (latex)
				s = escapeLaTeX(ssp_str[row].alias);
			else
				s = ssp_str[row].alias;

			os << "sc." << s;
			break;
		case REGISTER_IMM:
			os << "imm";
			break;
		default:
			os << "ERROR";
		}
		return os;
	}

	/** Print register specification table in LaTeX formatting.
	 * @param os Output stream for documentation output. */
	static void
	toLaTeX(ostream &os)
	{
		unsigned int i;

		os << "\\section{Register specifications}" << endl;
		os << "\\label{sec:isa_regspec}" << endl;
		os << endl;

		os << "Special vector registers:" << endl;
		os << endl;
		os << "\\begin{table}[H]" << endl;
		os << "\\begin{tabular}{p{0.6cm} p{2.8cm}|r|p{9cm}}" << endl;
		os << "Idx & Alias & Perm. & Description\\\\" << endl;
		os << "\\hline" << endl;
		for (i = 0; i < VSP_SENTINEL; i++) {
			os << i << " & ";
			AbstractRegister::print(os, REGISTER_VSP, i, true);
			os << " & " << (vsp_str[i].rw ? "rw" : "ro");
			os << " & " << vsp_str[i].doc << " \\\\" << endl;
		}
		os << "\\end{tabular}" << endl;
		os << "\\end{table}" << endl;
		os << endl;

		os << "Special scalar registers:" << endl;
		os << endl;
		os << "\\begin{table}[H]" << endl;
		os << "\\begin{tabular}{p{0.6cm} p{2.8cm}|r|p{9cm}}" << endl;
		os << "Idx & Alias & Perm. & Description\\\\" << endl;
		os << "\\hline" << endl;
		for (i = 0; i < SSP_SENTINEL; i++) {
			os << i << " & ";
			AbstractRegister::print(os, REGISTER_SSP, i, true);
			os << " & " << (ssp_str[i].rw ? "rw" : "ro");
			os << " & " << ssp_str[i].doc << " \\\\" << endl;
		}
		os << "\\end{tabular}" << endl;
		os << "\\end{table}" << endl;
		os << endl;
	}

	/** Return true iff this register is of a vector type.
	 * @return true iff this register is of a vector type.
	 */
	inline bool
	isVectorType(void) const
	{
		return AbstractRegister::isVectorType(type);
	}

	/** Stream output operator
	 * @param os Output stream.
	 * @param v AbstractRegister to print.
	 * @return Output stream.
	 */
	inline friend ostream &
	operator<<(ostream& os, AbstractRegister const &v)
	{
		AbstractRegister::print(os, v.type, v.row);
		return os;
	}

	/** Copy constructor
	 * @param r Register to copy from.
	 * @return A new AbstractRegister with a copy of this information.
	 */
	inline AbstractRegister &
	operator=(const AbstractRegister &r)
	{
		if (this == &r)
			return *this;

		type = r.type;
		wg = r.wg;
		row = r.row;

		return *this;
	}

	/** Equals operator.
	 * @param r Object to compare &this against.
	 * @return True iff these abstract registers are equal.
	 */
	inline bool
	operator==(const AbstractRegister &r)
	{
		if (type == REGISTER_NONE)
			return r.type == REGISTER_NONE;

		return (wg == r.wg && row == r.row && type == r.type);
	}

	/** Inequals operator.
	 * @param r Object to compare &this against.
	 * @return False iff these abstract registers are equal.
	 */
	inline bool
	operator!=(const AbstractRegister &r)
	{
		return !(*this == r);
	}

	/** SystemC mandatory trace output.
	 * @param tf Reference to trace file.
	 * @param v Control stack entry to print.
	 * @param NAME Name of the SystemC object.
	 */
	inline friend void
	sc_trace(sc_trace_file *tf, const AbstractRegister &v,
	    const std::string &NAME )
	{
		sc_trace(tf,v.row, NAME + ".row");
		sc_trace(tf,v.type, NAME + ".type");
		sc_trace(tf,v.wg, NAME + ".wg");
	}
};

/** A single register descriptor, doubles as a data struct for write requests.
 * @param COLS Number of possible columns for vector registers. */
template <unsigned int COLS = 8>
class Register : public AbstractRegister {
public:
	/** Which column of LANES lanes is addressed for this write operation.*/
	sc_uint<const_log2(COLS)> col;

	/** Default constructor. */
	Register() : AbstractRegister(0, REGISTER_NONE, 0), col(0) {}

	/** Constructor providing just workgroup.
	 * @param w Workgroup. */
	Register(sc_uint<1> w) : AbstractRegister(w, REGISTER_NONE, 0), col(0) {}

	/** Full constructor.
	 * @param w Workgroup
	 * @param t Type of register.
	 * @param r Row.
	 * @param c Column, forced to 0 for scalar registers. */
	Register(sc_uint<1> w, RegisterType t, sc_uint<const_log2(64)> r,
			sc_uint<const_log2(COLS)> c) : AbstractRegister(w, t, r)
	{
		switch (type) {
		case REGISTER_NONE:
		case REGISTER_SENTINEL:
		case REGISTER_SGPR:
		case REGISTER_IMM:
			col = 0;
			break;
		default:
			col = c;
			break;
		}
	}

	/** Clone this Register.
	 *
	 * Unlike when using the assignment operator, with clone() polymorphism
	 * helps to preserves column information. */
	AbstractRegister *
	clone(void) const
	{
		return new Register<COLS>(wg, type, row, col);
	}

	/** SystemC mandatory print stream operation.
	 * @param os Stream output.
	 * @param v Register to print. */
	inline friend ostream &
	operator<<(ostream& os, Register<COLS> const &v)
	{
		cout << "wg" << v.wg << ".";
		AbstractRegister::print(os, v.type, v.row);
		os << " COL(" << v.col << ")";
		return os;
	}

	/** SystemC mandatory trace output.
	 * @param tf Reference to trace file.
	 * @param v Control stack entry to print.
	 * @param NAME Name of the SystemC object.
	 */
	inline friend void
	sc_trace(sc_trace_file *tf, const Register<COLS> &v,
	    const std::string &NAME )
	{
		sc_trace(tf,v.row, NAME + ".row");
		sc_trace(tf,v.col, NAME + ".col");
		sc_trace(tf,v.type, NAME + ".type");
		sc_trace(tf,v.wg, NAME + ".wg");
	}

	/** Comparator.
	 * @param v Object to compare to this.
	 * @return true iff objects are equal.
	 */
	inline bool
	operator==(const Register<COLS> &v) const
	{
		switch (type) {
		case REGISTER_NONE:
			return v.type == REGISTER_NONE;
			break;
		case REGISTER_VGPR:
		case REGISTER_PR:
		case REGISTER_VSP:
			if (col != v.col)
				return false;
			break;
		default:
			break;
		}

		return (wg == v.wg && row == v.row && type == v.type);
	}

	/** Incomparator.
	 * @param v Object to compare to this.
	 * @return false iff objects are equal.
	 */
	inline bool
	operator!=(const Register<COLS> &v) const
	{
		return !(*this == v);
	}

	/** Copy constructor.
	 * @param v Object to copy data from into this object.
	 * @return New object with same data as v.
	 */
	inline Register<COLS> &
	operator=(const Register<COLS> &v)
	{
		col = v.col;
		row = v.row;
		type = v.type;
		wg = v.wg;
		return *this;
	}
};

}

#endif /* MODEL_REGISTER_H */
