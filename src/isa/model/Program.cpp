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

#include "isa/model/Program.h"
#include "isa/model/Metadata.h"
#include "util/defaults.h"

using namespace isa_model;
using namespace simd_model;
using namespace dram;

Program::Program()
: section(SECTION_DATA), bb_count(0), pc(0), cur_bb(nullptr),
  value_annotation_line(-1), value_annotation(0), sd_words(1), sd_period(1),
  sd_period_cnt(1), md(nullptr)
{
	cur_bb = new BB(bb_count++);

	vrf_wr = new bool[64]();
	srf_wr = new bool[32]();
	prf_wr = new bool[4]();
}

Program::~Program()
{
	BB *bb;

	while (bbs.size()) {
		bb = bbs.back();
		bbs.pop_back();
		delete bb;
	}

	delete[] vrf_wr;
	delete[] srf_wr;
	delete[] prf_wr;
}

bool
Program::is_section(string &s, string &section)
{
	unsigned int i;

	for (i = 0; i < s.size(); i++) {
		switch (s[i]) {
		case '\t':
		case ' ':
			break;
		case '.':
			if (i+1 < s.size()) {
				section = extract_id(s.substr(i+1));
				return section != "";
			}
			return false;
			break;
		default:
			return false;
			break;
		}
	}

	return false;
}

void
Program::parse_data(unsigned int l, string &s)
{
	unsigned int buf_id;
	unsigned int addr;
	unsigned int dim_x;
	unsigned int dim_y;
	buffer_input_type btype = INPUT_NONE;
	string path = "";

	if (!read_uint(s, buf_id)) {
		warn(l, "Expected buffer id (unsigned int), got \"" + s + "\"");
		return;
	}

	if (buf_id > 31) {
		warn(l, "Buffer ID (" + to_string(buf_id) + ") exceeds limit of 32");
		return;
	}

	if (!read_uint(s, addr)) {
		warn(l, "Expected address (unsigned int), got \"" + s + "\"");
		return;
	}

	if (!read_uint(s, dim_x)) {
		warn(l, "Expected X dimension, got \"" + s + "\"");
		return;
	}

	if (!read_uint(s, dim_y)) {
		warn(l, "Expected Y dimension, got \"" + s + "\"");
		return;
	}

	if (section == SECTION_DATA) {
		/* From here we could read either a path to a buffer
		 * initialisation file... or a comment? Mandate quotation marks
		 * for a path to know when it ends. */
		skip_whitespace(s);
		if (s.size() > 0) {
			switch (s[0]) {
			case 'd':
				btype = DECIMAL_CSV;
				break;
			case 'f':
				btype = BINARY;
				break;
			case 'n':
				btype = INPUT_NONE;
				break;
			default:
				warn(l, "Unexpected buffer type \"" + to_string(s[0]) + "\"");
				return;
			}

			/* Skip over character */
			read_id(s, path);

			read_path(s, path);
		}

		skip_whitespace(s);

		buffers[buf_id] = ProgramBuffer(addr, dim_x, dim_y, btype, path);
	} else if (section == SECTION_SPDATA) {
		sp_buffers[buf_id] = ProgramBuffer(addr, dim_x, dim_y);
	} else {
		warn(l, "Unexpected buffer definition.");
	}
}

/* Annotations we likely need:
 * - conditional branch cycles
 * - DRAM stride info (sc.sd_[words|lines|...]
 * - Max pop?
 *
 * They need to fall under one ``metadata'' umbrella, but don't want to
 * type out that word every directive. Pragma <key> <value> is more customary,
 * but too generic a term - would include things like loop unroll directives.
 */
void
Program::parse_bound(unsigned int l, string &s)
{
	string label;

	if (!read_id(s, label)) {
		warn(l, "Invalid bound directive: missing key");
		return;
	}

	if (label == "value") {
		/* A single integer value should follow. */
		read_uint(s, value_annotation);
		value_annotation_line = l;
	} else {
		if (!md)
			md = new Metadata();

		try {
			md->updateFromString(label, s);
		} catch (invalid_argument &ex){
			warn(l, ex.what());
		}
	}

	return;
}

void
Program::metadata_SMOVSSP(Instruction *op)
{
	unsigned int value;
	Operand src0 = op->getSrc(0);
	Operand dst = op->getDst();

	if (dst.getType() != OPERAND_REG ||
	    dst.getRegisterType() != REGISTER_SSP)
			throw invalid_argument("smovssp writing to non-SSP register");

	if (src0.getType() == OPERAND_IMM) {
		value = src0.getValue();
	} else if (src0.getType() == OPERAND_REG) {
		if (value_annotation_line < 0)
			throw invalid_argument("Cannot infer value of smovssp"
				"write, please annotate");
		value = value_annotation;
	}

	switch (dst.getIndex()) {
	case SSP_SD_WORDS:
		sd_words = value;
		break;
	case SSP_SD_PERIOD:
		sd_period = value;
		break;
	case SSP_SD_PERIOD_CNT:
		sd_period_cnt = value;
		break;
	default:
		break;
	}

	value_annotation_line = -1;
}

void
Program::metadata_ldst(Instruction *op)
{
	Metadata *md;

	md = op->getMetadata();

	if (!md) {
		md = new Metadata();
		op->addMetadata(md);
	}

	md->setSDConstants(sd_words, sd_period, sd_period_cnt);
}

void
Program::parse_text(unsigned int l, string &s, bool metadata)
{
	string label;
	Instruction *op;
	Operand oper;
	unsigned int idx;

	skip_whitespace(s);

	if (read_char(s,'#')) {
		/** The equivalent of a pre-processor directive, without a
		 * pre-processor. Currently just used for WCET_analysis
		 * annotations. */
		if (!read_id(s, label)) {
			warn(l, "Invalid preprocessor directive: missing type "
					"information.");
			return;
		}

		if (label == "bound") {
			if (metadata)
				parse_bound(l, s);
		} else {
			warn(l, "Invalid preprocessor directive: unknown "
					"type.");
			return;
		}

		return;
	} else if (!read_id(s, label)) {
		warn(l, "Expecting instruction or branch target label, "
				"got \"" + s + "\"");
		return;
	}

	skip_whitespace(s);

	/* Branch target */
	if (s.size() > 0 && s[0] == ':') {
		if (is_reserved_const(label)) {
			warn(l, "Branch label ignored, reserved keyword " +
					label);
			return;
		}

		if (cur_bb && !cur_bb->empty()) {
			bbs.push_back(cur_bb);
			cur_bb = new BB(bb_count++, pc);
		}

		branch_targets[label] = cur_bb;
	} else {
		try {
			op = new Instruction(label, s, l);
		} catch (exception const &e) {
			warn(l, e.what());
			return;
		}

		if (op->getOp() == OP_SMOVSSP) {
			try{
				metadata_SMOVSSP(op);
			} catch (invalid_argument &e) {
				warn(l, e.what());
			}
		}

		oper = op->getDst();
		switch (oper.getRegisterType()) {
		case REGISTER_VGPR:
			for (idx = 0; idx < op->getConsecutiveDstRegs(sd_words); idx++)
				vrf_wr[oper.getIndex() + idx] = true;
			break;
		case REGISTER_SGPR:
			for (idx = 0; idx < op->getConsecutiveDstRegs(sd_words); idx++)
				srf_wr[oper.getIndex() + idx] = true;
			break;
		case REGISTER_PR:
			prf_wr[oper.getIndex()] = true;
			break;
		default:
			break;
		}

		if (metadata) {
			/* Attach metadata to instruction. */
			op->addMetadata(md);
			md = nullptr;

			if (opCategory(op->getOp()) == CAT_LDST)
				metadata_ldst(op);

			if (value_annotation_line >= 0) {
				warn(value_annotation_line,
						"Spurious value annotation");
				value_annotation_line = -1;
			}
		}

		cur_bb->add_instruction(op);
		pc++;

		if (op->bbFinish()) {
			bbs.push_back(cur_bb);
			cur_bb = new BB(bb_count++, pc);
		}
	}
}

void
Program::parse(fstream &fs, bool metadata)
{
	string line, label;
	unsigned int l;

	l = 0;

	while (getline(fs, line)) {
		l++;

		if (is_whitespace(line))
			continue;

		if (is_section(line, label)) {
			if (label == "data")
				section = SECTION_DATA;
			else if (label == "sp_data")
				section = SECTION_SPDATA;
			else if (label == "text")
				section = SECTION_TEXT;
			else
				warn(l, "unknown label \"" + label + "\"");

			continue;
		}

		if (section == SECTION_TEXT)
			parse_text(l, line, metadata);
		else
			parse_data(l, line);
	}

	if (cur_bb && !cur_bb->empty())
		bbs.push_back(cur_bb);
	else
		bb_count--;

	cur_bb = nullptr;
}

void
Program::resolve_branch_targets(void)
{
	list<Instruction *>::iterator it;
	list<Instruction *>::reverse_iterator rit;

	for (BB *bb : bbs) {
		for (it = bb->begin(); it != bb->end(); it++) {
			resolve_branch_targets(*it);
		}
	}
}

void
Program::resolve_branch_targets(Instruction *insn)
{
	string target;
	BB *bb;
	unsigned int i;

	for (i = 0; i < insn->getSrcs(); i++) {
		Operand &op = insn->getSrc(i);

		if (op.getType() != OPERAND_BRANCH_TARGET)
			continue;

		target = op.getBranchTarget();
		bb = branch_targets[target];
		if (!bb)
			throw invalid_argument("Unknown branch target "
					"\"" + target + "\"");
		op.resolveBranchTarget(bb);
	}
}

bool
Program::validate_buffers(void) const
{
	bool retval;
	unsigned int i, j;
	sc_uint<32> i_end;
	sc_uint<32> j_end;

	retval = true;

	for (i = 0; i < 32; i++) {
		if (!buffers[i].valid)
			continue;

		i_end = buffers[i].addr +
			((buffers[i].dims[0] * buffers[i].dims[1]) << 2);

		for (j = i + 1; j < 32; j++) {
			if (!buffers[j].valid)
				continue;

			j_end = buffers[j].addr +
				((buffers[j].dims[0] * buffers[j].dims[1]) << 2);

			if ((buffers[i].addr <= buffers[j].addr && i_end > buffers[j].addr) ||
			    (buffers[j].addr <= buffers[i].addr && j_end > buffers[i].addr)) {
				warn(0, "Overlapping buffers " + to_string(i) +
						" and " + to_string(j));
				retval = false;
			}
		}
	}

	return retval;
}

const ProgramBuffer *
Program::buffer_begin() const
{
	return buffers.begin();
}


const ProgramBuffer *
Program::buffer_end() const
{
	return buffers.end();
}


const ProgramBuffer *
Program::sp_buffer_begin() const
{
	return sp_buffers.begin();
}


const ProgramBuffer *
Program::sp_buffer_end() const
{
	return sp_buffers.end();
}

vector<BB *>::const_iterator
Program::cbegin() const
{
	return bbs.cbegin();
}

vector<BB *>::const_iterator
Program::cend() const
{
	return bbs.cend();
}

vector<BB *>::const_reverse_iterator
Program::crbegin() const
{
	return bbs.crbegin();
}

vector<BB *>::const_reverse_iterator
Program::crend() const
{
	return bbs.crend();
}

BB *
Program::getBB(unsigned int i) const
{
	if (i >= bb_count)
		return nullptr;

	return bbs[i];
}

unsigned int
Program::getBBCount(void) const
{
	return bb_count;
}

unsigned int
Program::countInstructions(void) const
{
	unsigned int insns;

	insns = 0;
	for (BB *bb : bbs)
		insns += bb->countInstructions();

	return insns;

}

void
Program::addLoop(Loop *l)
{
	outer_loops.push_back(l);
}

ProgramBuffer &
Program::getBuffer(unsigned int i)
{
	return buffers[i];
}


ProgramBuffer &
Program::getSpBuffer(unsigned int i)
{
	return sp_buffers[i];
}

void
Program::print_reg_usage(void)
{
	unsigned int count;
	unsigned int i;
	size_t sp_total_size = 0;

	cout << "= Resource usage:" << endl;

	count = 0;
	for (i = 0; i < 64; i++) {
		if (vrf_wr[i])
			count++;
	}

	cout << "Vector registers        : " << count << endl;
	//cout << count << " & ";

	count = 0;
	for (i = 0; i < 32; i++) {
		if (srf_wr[i])
			count++;
	}

	cout << "Scalar registers        : " << count << endl;
	//cout << count << " & ";

	count = 0;
	for (i = 0; i < 4; i++) {
		if (prf_wr[i])
			count++;
	}

	cout << "Predicate registers     : " << count << endl;
	//cout << count << " & " ;

	count = 0;
	for (i = 0; i < MC_BIND_BUFS; i++)
		if (buffers[i].valid)
			count++;
	cout << "Bound DRAM buffers      : " << count << endl;

	count = 0;
	for (i = 0; i < MC_BIND_BUFS; i++)
		if (sp_buffers[i].valid) {
			count++;
			sp_total_size += (sp_buffers[i].size());
		}

	cout << "Bound scratchpad buffers: " << count << endl;
	cout << "Total scratchpad size/wg: " << sp_total_size << " B"
			<< endl;

	//cout << sp_total_size << " & ";

	//cout << linearise_code().size() << "\\\\" << endl;
}

vector<Loop *>::const_iterator
Program::loops_cbegin(void)
{
	return outer_loops.cbegin();
}


vector<Loop *>::const_iterator
Program::loops_cend(void)
{
	return outer_loops.cend();
}

void
Program::print_buffers(void) const
{
	unsigned int i;

	cout << "= Buffers:" << endl;
	for (i = 0; i < 32; i++) {
		if (buffers[i].valid)
			cout << i << ": " << buffers[i] << endl;
	}
}


void
Program::print_sp_buffers(void) const
{
	unsigned int i;

	cout << "= Scratchpad buffers:" << endl;
	for (i = 0; i < 32; i++) {
		if (sp_buffers[i].valid)
			cout << i << ": " << sp_buffers[i] << endl;
	}
}

void
Program::print_branch_targets(void)
{
	cout << "= Branch targets:" << endl;
	for (auto t : branch_targets) {
		cout << t.first << ": " << *t.second << endl;
	}
}

void
Program::print_loops(void) const
{
	cout << "= Loops:" << endl;
	cout << "Top-level: " << outer_loops.size() << endl;
	for (auto l : outer_loops)
		cout << *l;
}

void
Program::print(void)
{
	list<Instruction *>::iterator it;
	unsigned int i = 0;

	cout << "= Program:" << endl;
	for (BB *bb : bbs) {
		cout << *bb << endl;
		bb->printCFG(cout);
		for (it = bb->begin(); it != bb->end(); it++) {
			cout << "\t" << i << ": " << **it << endl;
			i++;
		}
	}
}

vector<Instruction *>
Program::linearise_code(void)
{
	vector<Instruction *> v;
	list<Instruction *>::iterator it;

	for (BB *bb : bbs) {
		for (it = bb->begin(); it != bb->end(); it++) {
			v.push_back(*it);
		}
	}

	return v;
}

