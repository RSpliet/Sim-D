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

#include <fstream>

#include "isa/model/Instruction.h"

using namespace isa_model;

void
doPrint(ostream *s)
{
	unsigned int i, op;

	*s << "\\chapter{ISA}" << endl;
	*s << "\\label{ch:isa}" << endl;
	*s << endl;
	*s << "\\newcommand{\\insn}[3]{" << endl;
	*s << "\t\\subsection{#1}" << endl;
	*s << "\t\\label{isa_insn:#1}" << endl;
	*s << "\t#2" << endl;
	*s << endl;
	*s << "\t\\begin{table}[H]" << endl;
	*s << "\t\\begin{tabular}{l l}" << endl;
	*s << "\t\\textbf{Syntax} & \\parbox[t]{13cm}{#3}" << endl;
	*s << "\t\\end{tabular}" << endl;
	*s << "\t\\end{table}" << endl;
	*s << "}" << endl;

	*s << endl;
	*s << "\\section{Conventions}" << endl;
	*s << "\\label{sec:isa_conv}" << endl;
	*s << "For all instructions, an ``s'' prefix denotes a scalar "
		"instruction. The ``i'' prefix is used for integer arithmetic. "
		"When no prefix is given, the instruction is either a floating "
		"point or untyped vector instruction."
		<< endl;
	*s << endl;
	*s << "Optional operands are denoted between [brackets]." << endl;
	*s << endl;
	*s << "Special purpose vector and scalar registers can be referred to "
		"either by their alias, e.g. vc.tid\\_x, or by their index, e.g. "
		"vc4. We recommend the use of aliassed registers for code "
		"readability. A full list of all special purpose registers is "
		"given in Section~\\ref{sec:isa_regspec}." << endl;

	AbstractRegister::toLaTeX(*s);

	for (i = 0; i < CAT_SENTINEL; i++) {
		*s << endl;
		*s << "\\section{" << cat_str[i] << "}" << endl;

		for (op = 0; op < OP_SENTINEL; op++) {
			if (opCategory(ISAOp(op)) == i) {
				printOp(ISAOp(op), s);
			}
		}
	}
}

int
main(int argc, char **argv)
{
	ostream *s = &cout;

	if (argc > 1)
		s = new ofstream(argv[1]);

	doPrint(s);

	return 0;
}
