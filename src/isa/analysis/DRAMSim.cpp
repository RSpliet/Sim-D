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

#include "isa/analysis/DRAMSim.h"
#include "util/constmath.h"
#include "util/debug_output.h"

using namespace isa_model;
using namespace simd_model;
using namespace dram;

namespace isa_analysis {

static stride_descriptor
strideLDSTGLIN(Program &p, workgroup_width w, Instruction *op)
{
	stride_descriptor sd;
	sc_uint<32> wg_width;
	unsigned int bidx;
	sc_uint<32> wl;

	ISASubOpLDSTLIN subop = op->getSubOp().ldstlin;

	wg_width = 32 << w;
	bidx = op->getSrc(0).getValue();
	Buffer &b = p.getBuffer(bidx);

	sd.dst = RequestTarget(0,TARGET_SP);

	switch (subop) {
	case LIN_VEC2:
		wl = 2;
		sd.idx_transform = IDX_TRANSFORM_VEC2;
		break;
	case LIN_VEC4:
		wl = 4;
		sd.idx_transform = IDX_TRANSFORM_VEC4;
		break;
	case LIN_UNIT:
	default:
		wl = 1;
		sd.idx_transform = IDX_TRANSFORM_UNIT;
		break;
	}

	sd.write = (op->getOp() == OP_STGLIN);
	sd.period = b.get_dim_x();
	sd.period_count = sc_uint<32>(COMPUTE_THREADS/wg_width);
	sd.words = min(sc_uint<32>(wl * wg_width), b.get_dim_x());
	sd.dst_period = sd.words;

	sd.dst_offset = 0;
	sd.addr = b.getAddress();

	return sd;
}

static stride_descriptor
strideSLDG(Program &p, Instruction *op)
{
	stride_descriptor sd;
	unsigned int bidx;
	unsigned int words;

	bidx = op->getSrc(0).getValue();
	if (op->getSrcs() > 1)
		words = op->getSrc(1).getValue();
	else
		words = 1;
	Buffer &b = p.getBuffer(bidx);

	sd.dst = RequestTarget(0,TARGET_SP);

	sd.write = false;
	sd.period = words;
	sd.period_count = 1;
	sd.words = words;
	sd.dst_period = words;

	sd.dst_offset = 0;
	sd.addr = b.getAddress();

	return sd;
}

static stride_descriptor
strideLDSTGCIDX(Program &p, Instruction *op)
{
	stride_descriptor sd;
	unsigned int bidx;
	Metadata *md;

	md = op->getMetadata();
	if (md == nullptr) {
		throw invalid_argument("Missing metadata for LD/STGCIDX operation.");
	}

	bidx = op->getSrc(0).getValue();
	Buffer &b = p.getBuffer(bidx);

	sd.dst = RequestTarget(0,TARGET_SP);

	sd.write = (op->getOp() == OP_STGCIDX);
	sd.period = md->getSDPeriod();
	sd.period_count = md->getSDPeriodCnt();
	sd.words = md->getSDWords();
	sd.dst_period = sd.words;

	sd.dst_offset = 0;
	sd.addr = b.getAddress();

	return sd;
}

static stride_descriptor
strideLDSTG2SPTILE(Program &p, Instruction *op)
{
	stride_descriptor sd;
	unsigned int bidx;

	bidx = op->getSrc(0).getValue();
	Buffer &b = p.getBuffer(bidx);
	bidx = op->getDst().getValue();
	Buffer &bsp = p.getSpBuffer(bidx);

	sd.dst = RequestTarget(0,TARGET_SP);

	sd.write = (op->getOp() == OP_STG2SPTILE);
	sd.period = b.get_dim_x();
	sd.period_count = bsp.get_dim_y();
	sd.words = bsp.get_dim_x();
	sd.dst_period = bsp.get_dim_x();

	sd.dst_offset = 0;
	sd.addr = b.getAddress();

	return sd;
}

static unsigned long
boundLDSTGBIDX(Program &p, Instruction *op,
		const dram::dram_timing *dram)
{
	unsigned int bidx;
	size_t bs;
	unsigned long bound;

	bidx = op->getSrc(0).getValue();
	Buffer &b = p.getBuffer(bidx);

	/** XXX: Actually we know our alignment. Can derive a tighter bound. */
	bs = bursts(dram, b.dims[0] * b.dims[1] * 4, false);
	if (op->getOp() == OP_LDGBIDX)
		bound = least_issue_delay_rd_ddr4(dram, bs, false);
	else
		bound = least_issue_delay_wr_ddr4(dram, bs, false);

	return bound;
}

static unsigned long
boundLDSTSPBIDX(Program &p, Instruction *op)
{
	unsigned int bidx;
	unsigned int words;
	unsigned long bound;

	bidx = op->getSrc(0).getValue();
	Buffer &b = p.getSpBuffer(bidx);

	words = b.get_dim_x() * b.get_dim_y();
	/* Round up, add 1 for pipeline delay */
	bound = ((words + SP_BUS_WIDTH - 1) / SP_BUS_WIDTH) + 1;

	return bound;
}

static unsigned long
boundSLDSP(Instruction *op)
{
	unsigned int words;
	unsigned long bound;
	Metadata *md;

	md = op->getMetadata();
	if (md == nullptr) {
		throw invalid_argument("Missing metadata for SLDSP operation.");
	}

	words = md->getSDWords();
	/* Round up, add 1 for pipeline delay */
	bound = ((words + SP_BUS_WIDTH - 1) / SP_BUS_WIDTH) + 1;

	return bound;
}

static unsigned long
boundLDSTGIDXIT(Program &p, const dram_timing *dram, Instruction *op)
{
	unsigned int bidx;
	unsigned int words;
	unsigned long bound;

	bidx = op->getSrc(0).getValue();
	Buffer &b = p.getBuffer(bidx);
	words = b.get_dim_x() * b.get_dim_y();

	if (op->getOp() == OP_LDGIDXIT)
		bound = least_issue_delay_idxit_rd_ddr4(dram, words,
				COMPUTE_THREADS);
	else
		bound = least_issue_delay_idxit_wr_ddr4(dram, words,
				COMPUTE_THREADS);

	/* Add 3 for DRAM pipeline delay */
	bound += 3;

	return bound;
}

static unsigned long
spStrides(unsigned int words, unsigned int period,
		unsigned int period_cnt)
{
	unsigned int pos;
	uint64_t i;
	unsigned int have_skipped = 0;
	unsigned int skip;
	unsigned int skipcnt;
	unsigned int skiprst;
	unsigned long reads = 0ul;
	unsigned int end;

	skipcnt = (period - (words + SP_BUS_WIDTH - 1));
	skiprst = (skipcnt & (SP_BUS_WIDTH - 1));

	end = period * (period_cnt - 1) + words;

	skipcnt &= ~(SP_BUS_WIDTH - 1);

	for (i = 0; i < end; i++) {
		pos = i % period;

		if (i % SP_BUS_WIDTH == 0) {
			reads++;
		}

		if (pos < words) {
			have_skipped = 0;
		}

		if (i % SP_BUS_WIDTH == SP_BUS_WIDTH - 1) {
			/* SKIP */
			if (!have_skipped) {
				skip = 0;
				if (pos >= words-1) {
					skip = skipcnt;
					if (pos < words+skiprst - 1)
						skip += SP_BUS_WIDTH;
				}
				if (!skip)
					continue;

				i += skip;
				have_skipped = 1;
			}
		}
	}

	return reads;
}

static unsigned long
boundLDSTSPLIN(Program &p, workgroup_width w, Instruction *op)
{
	unsigned int bidx;
	unsigned int words;
	unsigned int period;
	unsigned int period_cnt;

	bidx = op->getSrc(0).getValue();
	Buffer &b = p.getSpBuffer(bidx);

	words = 32 << w;
	period = b.get_dim_x();
	period_cnt = COMPUTE_THREADS / words;

	if (words > period)
		throw invalid_argument("Workgroup wider than scratchpad buffer.");

	/* Round up, add 1 for pipeline delay */
	return spStrides(words, period, period_cnt) + 1;
}

unsigned long
ProgramUploadTime(Program &p, const dram_timing *dram)
{
	size_t b;
	unsigned long dram_cycles;
	unsigned long bytes;

	bytes = p.countInstructions() * 8;

	b = bursts(dram, bytes, 1);
	dram_cycles = least_issue_delay_rd_ddr4(dram, b, 1);

	/* And convert to SimdCluster cycles
	 * @todo Less static */
	return ((dram_cycles * 1000) + (dram->clkMHz-1)) / dram->clkMHz;
}

void
DRAMSim(Program &p, workgroup_width w, const dram_timing *dram,
		unsigned long (*sim)(stride_descriptor &, bool))
{
	vector<BB *>::const_iterator bbit;
	BB *bb;
	Instruction *op;
	unsigned long bound;
	unsigned long bound_compute;
	Metadata *md;
	stride_descriptor sd;

	if (debug_output[DEBUG_WCET_PROGRESS])
		cout << "* DRAM cycle simulation." << endl;

	for (bbit = p.cbegin(); bbit != p.cend(); bbit++) {
		bb = *bbit;
		if (bb->empty())
			continue;

		op = *(bb->rbegin());

		switch (op->getOp()) {
		case OP_LDGLIN:
		case OP_STGLIN:
			sd = strideLDSTGLIN(p, w, op);
			if (debug_output[DEBUG_WCET_PROGRESS]) {
				cout << "  Sweep sim : " << *op << endl;
				cout << "    - Stride: " << sd << endl;
			}

			bound = sim(sd, true);
			break;
		case OP_SLDG:
			sd = strideSLDG(p, op);
			if (debug_output[DEBUG_WCET_PROGRESS]) {
				cout << "  Single sim: " << *op << endl;
				cout << "    - Stride: " << sd << endl;
			}

			bound = sim(sd, false);
			break;
		case OP_LDGBIDX:
		case OP_STGBIDX:
			if (debug_output[DEBUG_WCET_PROGRESS])
				cout << "  Static    : " << *op <<endl;
			/* DRAM pipeline delay of 3 cycles */
			bound = boundLDSTGBIDX(p, op, dram) + 3;
			break;
		case OP_LDGCIDX:
		case OP_STGCIDX:
			sd = strideLDSTGCIDX(p, op);
			if (debug_output[DEBUG_WCET_PROGRESS]) {
				cout << "  Sweep sim : " << *op << " (sweep sim)" <<endl;
				cout << "    - Stride: " << sd << endl;
			}

			bound = sim(sd, true);
			break;
		case OP_LDG2SPTILE:
		case OP_STG2SPTILE:
			sd = strideLDSTG2SPTILE(p, op);
			if (debug_output[DEBUG_WCET_PROGRESS]) {
				cout << "  Sweep sim : " << *op << endl;
				cout << "    - Stride: " << sd << endl;
			}

			bound = sim(sd, true);
			break;
		case OP_LDSPBIDX:
		case OP_STSPBIDX:
			if (debug_output[DEBUG_WCET_PROGRESS]) {
				cout << "  Sweep sim : " << *op << endl;
				cout << "    - Stride: " << sd << endl;
			}

			bound = boundLDSTSPBIDX(p, op);
			break;
		case OP_SLDSP:
			if (debug_output[DEBUG_WCET_PROGRESS])
				cout << "  Static    : " << *op << endl;
			bound = boundSLDSP(op);
			break;
		case OP_LDSPLIN:
		case OP_STSPLIN:
			if (debug_output[DEBUG_WCET_PROGRESS])
				cout << "  Static    : " << *op << endl;
			bound = boundLDSTSPLIN(p, w, op);
			break;
		case OP_LDGIDXIT:
		case OP_STGIDXIT:
			if (debug_output[DEBUG_WCET_PROGRESS])
				cout << "  Static    : " << *op << endl;

			bound = boundLDSTGIDXIT(p, dram, op);
			break;
		default:
			continue;
		}

		bound_compute = ((bound * 1000) + (dram->clkMHz-1))
					/ dram->clkMHz;

		md = op->getMetadata();

		if (md) {
			md = new Metadata();
			op->addMetadata(md);
		}

		md->setDRAMlid(bound, bound_compute);
	}
}

}
