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

#ifndef ISA_MODEL_PROGRAM_H
#define ISA_MODEL_PROGRAM_H

#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>
#include <systemc>

#include "isa/model/Instruction.h"
#include "isa/model/BB.h"
#include "isa/model/CFGEdge.h"
#include "isa/model/Loop.h"
#include "model/Buffer.h"
#include "model/stride_descriptor.h"
#include "util/parse.h"
#include "util/ddr4_lid.h"

using namespace std;
using namespace sc_dt;
using namespace simd_model;

namespace isa_model {

/** A Sim-D program. Contains a constructor that takes a file as an argument,
 * assembles its content. */
class Program
{
private:
	/** State: section currently being read. */
	enum {
		SECTION_DATA,
		SECTION_SPDATA,
		SECTION_TEXT
	} section;

	/** Number of BBs in the program. */
	unsigned int bb_count;

	/** List of all BBs in the program. */
	std::vector<BB *> bbs;

	/** List of all outer loops in the program. */
	std::vector<Loop *> outer_loops;

	/** Array of program buffers read from the program. */
	std::array<ProgramBuffer,32> buffers;

	/** Array of scratchpad program buffers read from the program. */
	std::array<ProgramBuffer,32> sp_buffers;

	/** PC of last read instruction */
	sc_uint<11> pc;

	/** Pointer to currently read BB. */
	BB *cur_bb;

	/** Control flow analysis state: Current state of stack. */
	std::vector<std::pair<unsigned int, BB *> > cstack;

	/** Maps labels found in source code to their target BB. */
	std::unordered_map<string, BB *> branch_targets;

	/** Vector registers being written to. */
	bool *vrf_wr;

	/** Scalar registers being written to. */
	bool *srf_wr;

	/** Predicate registers being written to. */
	bool *prf_wr;

	/******* Some bookkeeping for stride descriptor value tracking ******/
	/** Line in which a value was passed by annotation. */
	int value_annotation_line;

	/** The value passed by annotation. */
	unsigned int value_annotation;

	/** Last-read value for the sc.sd_words register. */
	unsigned int sd_words;
	/** Last-read value for the sc.sd_period register. */
	unsigned int sd_period;
	/** Last-read value for the sc.sd_period_cnt register. */
	unsigned int sd_period_cnt;

	/** Pointer to metadata structure used for static WCET analysis. */
	Metadata *md;

	/** Emit a warning.
	 * @param l Line of occurance of warning.
	 * @param s String to print as a warning. */
	inline void
	warn(unsigned int l, string s) const
	{
		cout << "Warning: " << s << " in line " << l << endl;
	}

	/** Return true iff this string is a section.
	 * @param s String to search section from.
	 * @param section Extracted section name.
	 * @return True iff a section name has been extracted.
	 */
	bool
	is_section(string &s, string &section);

	/** Parse string as a line in the data section.
	 *
	 * Expect a buffer specification in the form \<int\> \<physical addr\>.
	 * Comment lines and section headers are already stripped out. Rest of
	 * the line should be ignored, so safe to stick comments in.
	 * @todo Should also be able to preload data from file. */
	void
	parse_data(unsigned int l, string &s);

	/** Update last read value of sc.sd_* registers.
	 *
	 * This does not add a metadata structure to the instruction itself,
	 * merely updates the sd_* values in this Program object.
	 * @param op SMOVSSP instruction. */
	void metadata_SMOVSSP(Instruction *op);

	/** Update metadata with load/store information.
	 *
	 * Allocates a metadata structure and sets the sc.sd_* register values
	 * in this metadata structure such that a subsequent DRAM/scratchpad
	 * simulation pass can extract relevant parameters.
	 * @param op Load/store instruction. */
	void metadata_ldst(Instruction *op);

	/** Parse string as a line in the text section.
	 *
	 * Expecting either a branch target label, or an instruction.
	 * Strategy is to test for a branch target label. If not found, pass
	 * string on to the Instruction constructor.
	 * @param l Line number
	 * @param s String representing the full line
	 * @param metadata True iff metadata must be processed (WCET analysis),
	 * 		   false otherwise (simulation run).
	 */
	void parse_text(unsigned int l, string &s, bool metadata);

	/** Parse WCET value/branch bound annotations.
	 * @param l Line number
	 * @param s String representing the full line */
	void parse_bound(unsigned int l, string &s);

	/** Resolve the branch targets for given instruction.
	 *
	 * This function can only be called after *all* branch targets
	 * have been added to the branch_targets maps, e.g. after the entire
	 * input file was read and all instructions, BBs and branch targets
	 * have been read.
	 * @param insn Instruction to resolve branch targets for.
	 */
	void resolve_branch_targets(Instruction *insn);

public:
	/** Constructor. Parses the provided file and generates a program. */
	Program();

	/** Default destructor. */
	~Program();

	/** Main fstream parse function
	 * @param fs File stream of (opened) assembly file.
	 * @param metadata True iff metadata should be stored (WCET analysis),
	 * 		   false otherwise (simulation run).
	 */
	void parse(fstream &fs, bool metadata = false);

	/** Resolve the branch targets for given instruction.
	 *
	 * This function can only be called after *all* branch targets
	 * have been added to the branch_targets maps, e.g. after the entire
	 * input file was read and all instructions, BBs and branch targets
	 * have been read.
	 */
	void resolve_branch_targets(void);

	/** Return a linearised code stream.
	 * @return A vector of all instructions in order. */
	vector<Instruction *> linearise_code(void);

	/** Validate buffers to make sure they don't overlap.
	 * @return True iff buffers are valid. */
	bool validate_buffers(void) const;

	/** Return a requested DRAM ProgramBuffer mapping.
	 * @param i Index of the DRAM buffer.
	 * @return Reference to the corresponding ProgramBuffer. */
	ProgramBuffer &getBuffer(unsigned int i);

	/** Return a requested scratchpad ProgramBuffer mapping.
	 * @param i Index of the scratchpad buffer.
	 * @return Reference to the corresponding ProgramBuffer. */
	ProgramBuffer &getSpBuffer(unsigned int i);

	/** Pointer to start of the DRAM buffers array.
	 * @return Pointer to the first program buffer. */
	const ProgramBuffer *buffer_begin() const;

	/** Pointer to end of the DRAM buffers array.
	 * @return Pointer to the last program buffer. */
	const ProgramBuffer *buffer_end() const;

	/** Pointer to start of the scratchpad buffers array.
	 * @return Pointer to the first program buffer. */
	const ProgramBuffer *sp_buffer_begin() const;

	/** Pointer to end of the scratchpad buffers array.
	 * @return Pointer to the last program buffer. */
	const ProgramBuffer *sp_buffer_end() const;

	/** Basic Block constant iterator begin.
	 * @return constant iterator to first BB. */
	vector<BB *>::const_iterator cbegin() const;

	/** Basic Block constant iterator end.
	 * @return Dummy end constant iterator. */
	vector<BB *>::const_iterator cend() const;

	/** Basic Block constant iterator begin, iterating the list in reverse
	 * order.
	 * @return constant iterator to first BB. */
	vector<BB *>::const_reverse_iterator crbegin() const;

	/** Basic Block constant iterator end, iterating the list in reverse
	 * order.
	 * @return Dummy end constant iterator. */
	vector<BB *>::const_reverse_iterator crend() const;

	/** Return the BB corresponding with a given identifier
	 * @param i BB ID
	 * @return pointer to the corresponding BB. */
	BB *getBB(unsigned int i) const;

	/** Get the number of BBs in this Program.
	 * @return the number of BBs in this Program. */
	unsigned int getBBCount(void) const;

	/** Get the number of instructions in this Program.
	 * @return the number of instructions in this Program. */
	unsigned int countInstructions(void) const;

	/** Add an outer loop to the list of loops for this program.
	 * @param l Loop object to add. */
	void addLoop(Loop *l);

	/** Return a constant iterator for the list of loops.
	 * @return constant iterator for the list of loops of this program. */
	std::vector<Loop *>::const_iterator loops_cbegin(void);

	/** Return a constant iterator to the (dummy) end of the list of loops.
	 * @return constant iterator for the list of loops. */
	std::vector<Loop *>::const_iterator loops_cend(void);

	/** Debugging output: print all buffers for given program */
	void print_buffers(void) const;

	/** Debugging output: print all scratchpad buffers for given program */
	void print_sp_buffers(void) const;

	/** Debugging output: print all branch targets */
	void print_branch_targets(void);

	/** Debugging output: print register usage. */
	void print_reg_usage(void);

	/** Debugging output: print all loops. */
	void print_loops(void) const;

	/** Debugging output: print the program. */
	void print(void);
};

}

#endif /* ISA_MODEL_PROGRAM_H */
