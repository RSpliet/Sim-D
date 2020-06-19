/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2018-2020 Roy Spliet, University of Cambridge
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

#ifndef COMPUTE_CONTROL_REGFILE_H
#define COMPUTE_CONTROL_REGFILE_H

#include <cstdint>
#include <array>

#include "compute/model/work.h"
#include "compute/model/compute_stats.h"
#include "compute/control/RegHazardDetect_3R1W.h"
#include "isa/model/Operand.h"
#include "model/request_target.h"
#include "model/stride_descriptor.h"
#include "util/constmath.h"
#include "util/debug_output.h"

using namespace std;
using namespace sc_core;
using namespace sc_dt;
using namespace compute_model;
using namespace simd_model;
using namespace isa_model;

namespace compute_control {

/**
 * A register file. Instantiates the various banks and routes signals
 * accordingly.
 *
 * By default, behaves as a 3R1W register file with one bank per-workgroup. This
 * can be overridden by replacing the hazard_detect member with an
 * implementation for a different hazard detection policy.
 * @param THREADS Number of threads in a work-group. Must be a power of two.
 * @param LANES Number of physical SIMD lanes. Must be a power of two.
 */
template <unsigned int THREADS, unsigned int LANES, unsigned int BUS_WIDTH,
		unsigned int BUS_WIDTH_SP>
class RegFile : public sc_module
{
private:
	/** Reference to storage for register data for Vector Register File. */
	sc_uint<32> *VRF[2];
	/** Reference to storage for register data for Scalar Register File. */
	sc_uint<32> *SRF[2];
	/** Reference to storage for data for Predicate Register File. */
	sc_uint<1> *PRF[2];

	/** Cam index values */
	sc_uint<30> *cam_idx[2];

	/** Cam buffer values */
	sc_uint<32> *cam_val[2];

	/**
	 * Storage for Control Mask Register File.
	 * We need to assume that these registers are independent flip-flops
	 * rather than SRAMs, so cheap to read for special purposes.
	 */
	sc_bv<LANES> CMRF[2][4][THREADS/LANES];

	/** Signal that indicates for each thread whether it's active. */
	sc_bv<LANES> lanes_en[2][THREADS/LANES];

	/** Hazard (bank conflict) detection logic. */
	RegHazardDetect<THREADS,LANES> *hazard_detect;

	/** Number of words read from the VRF through the DRAM interface. */
	unsigned long dram_vrf_words_r;
	/** Number of words written to the VRF through the DRAM interface. */
	unsigned long dram_vrf_words_w;

	/** Net number of bytes read from the VRF through the DRAM interface. */
	unsigned long dram_vrf_net_words_r;
	/** Net number of bytes written to the VRF through the DRAM interface.*/
	unsigned long dram_vrf_net_words_w;

	/** The word size of a vector register bank. */
	unsigned int vrf_bank_words;

	/** Map tracking VRF banks hit.
	 *
	 * This provides an interesting data point for optimisation of vector
	 * register files. If we know how many banks are hit by a DRAM request
	 * we can potentially estimate the power consumption for different bank
	 * configurations.
	 */
	bool *vrf_bank_word_hit_map;

	/** Shadow register: at least one thread is active. */
	sc_bv<2> thread_active;

	/** Shadow register: all threads are finished. */
	sc_bv<2> threads_fini;

public:
	/** Compute clock. */
	sc_in<bool> in_clk{"in_clk"};

	/** DRAM input clock */
	sc_in<bool> in_clk_dram{"in_clk_dram"};

	/** Read requests for this cycle
	 * We use a fifo here instead of a regular sc_in to enforce order of
	 * execution. IDecode will generate the register coordinates, and
	 * RegFile will fetch those in the same cycle. For correct functioning,
	 * in_req must be written to *exactly once every cycle*, even when no
	 * data is requested. Failure to do so will result in loss of writeback
	 * operations, which are regular register-backed signals.
	 */
	sc_fifo_in<reg_read_req<THREADS/LANES> > in_req_r{"in_req_r"};

	/** Data read from registers */
	sc_inout<sc_uint<32> > out_data_r[3][LANES];

	/** Bank conflicts for read ops. */
	sc_fifo_out<sc_bv<3> > out_req_conflicts{"out_req_conflicts"};

	/** Write request for this cycle. */
	sc_in<Register<THREADS/LANES> > in_req_w{"in_req_w"};

	/** Data in for write operations. */
	sc_in<sc_uint<32> > in_data_w[LANES];

	/** Mask determining which registers should be written.*/
	sc_in<sc_bv<LANES> > in_mask_w{"in_mask_w"};

	/** Write enable bit. */
	sc_in<bool> in_w{"in_w"};

	/** Last warp executing for the active workgroup.
	 * XXX: use information from the source instead. */
	sc_in<sc_uint<const_log2(THREADS/LANES)> > in_last_warp[2];

	/** Workgroup for mask register output. */
	sc_in<sc_uint<1> > in_wg_mask_w{"in_wg_mask_w"};

	/** Column for mask register output */
	sc_fifo_in<sc_uint<const_log2(THREADS/LANES)> >
				in_col_mask_w{"in_col_mask_w"};

	/** Write mask for column in_col_mask_w */
	sc_inout<sc_bv<LANES> > out_mask_w{"out_mask_w"};

	/** Ignore the provided thread mask. Used when writing to the control
	 * mask registers when executing e.g. CPOP. */
	sc_in<bool> in_ignore_mask_w{"in_ignore_mask_w"};

	/** At least one thread is active. */
	sc_inout<sc_bv<2> > out_thread_active{"out_thread_active"};

	/** Workgroup finished execution. */
	sc_inout<sc_bv<2> > out_wg_finished{"out_wg_finished"};

	/***************** Write channel for "inactive" WG *****************/
	/** @todo Where are these signals coming from */
	/** Reset CMASK for given workgroup */
	sc_in<bool> in_cmask_rst{"in_cmask_rst"};

	/** Workgroup to reset CMASK for */
	sc_in<sc_uint<1> > in_cmask_rst_wg{"in_cmask_rst_wg"};

	/** Workgroup offsets (X, Y) */
	sc_in<sc_uint<32> > in_wg_off[2][2];

	/** Work dimensions (X, Y) */
	sc_in<sc_uint<32> > in_dim[2];

	/** Dimension of the workgroups */
	sc_in<workgroup_width> in_wg_width{"in_wg_width"};

	/************** Write channel from Storage sources *****************/
	/** Data bus enabled. */
	sc_in<bool> in_store_enable[IF_SENTINEL];

	/** Operation is a Register write op. */
	sc_in<bool> in_store_write[IF_SENTINEL];

	/** Register description of (first) data word element */
	sc_in<AbstractRegister> in_store_reg[IF_SENTINEL];

	/** Write mask. */
	sc_in<sc_bv<BUS_WIDTH/4> > in_dram_store_mask{"in_dram_store_mask"};

	/** Indexes for each incoming data word */
	sc_in<reg_offset_t<THREADS> > in_dram_store_idx[BUS_WIDTH/4];

	/** Data from different storage systems (SP, DRAM). */
	sc_in<sc_uint<32> > in_dram_store_data[BUS_WIDTH/4];

	/** Outgoing data to DRAM */
	sc_inout<sc_uint<32> > out_dram_store_data[BUS_WIDTH/4];

	/** Write mask taking into account individual lane status */
	sc_inout<sc_bv<BUS_WIDTH/4> > out_dram_store_mask{"out_dram_store_mask"};

	/** Write mask. */
	sc_in<sc_bv<BUS_WIDTH_SP> > in_sp_store_mask[2];

	/** Indexes for each incoming data word */
	sc_in<reg_offset_t<THREADS> > in_sp_store_idx[2][BUS_WIDTH_SP];

	/** Data from different storage systems (SP, DRAM). */
	sc_in<sc_uint<32> > in_sp_store_data[2][BUS_WIDTH_SP];

	/** Outgoing data to DRAM */
	sc_inout<sc_uint<32> > out_sp_store_data[2][BUS_WIDTH_SP];

	/** Write mask taking into account individual lane status */
	sc_inout<sc_bv<BUS_WIDTH_SP> > out_sp_store_mask[2];

	/** Trigger an index push. */
	sc_in<bool> in_store_idx_push_trigger{"in_store_idx_push_trigger"};

	/** Providing indexes to the StrideSequencers index iterator. */
	sc_fifo_out<idx_t<THREADS> > out_store_idx{"out_store_idx"};

	/*********************** DRAM specific **************************/
	/** Destination targeted by DRAM request. */
	sc_in<RequestTarget> in_dram_dst{"in_dram_dst"};

	/**************** Stride-pattern special registers*************/
	/** Stride-descriptor special register values.
	 * @todo Wasteful in terms of memory. Also, if we convey offsets as
	 * (x,y) pairs rather than a single counter, we might need a dedicated
	 * struct. */
	sc_inout<stride_descriptor> out_sd[2];

	/** Construct thread. */
	SC_CTOR(RegFile)
	: hazard_detect(new RegHazardDetect_3R1W<THREADS,LANES>()),
	  dram_vrf_words_r(0ul), dram_vrf_words_w(0ul),
	  dram_vrf_net_words_r(0ul), dram_vrf_net_words_w(0ul),
	  vrf_bank_word_hit_map(nullptr)
	{
		sc_bv<LANES> bv = 0;
		bv.b_not();

		VRF[0] = new sc_uint<32>[THREADS*64];
		VRF[1] = new sc_uint<32>[THREADS*64];
		SRF[0] = new sc_uint<32>[32];
		SRF[1] = new sc_uint<32>[32];
		PRF[0] = new sc_uint<1>[THREADS*4];
		PRF[1] = new sc_uint<1>[THREADS*4];

		cam_idx[0] = new sc_uint<30>[THREADS];
		cam_idx[1] = new sc_uint<30>[THREADS];
		cam_val[0] = new sc_uint<32>[THREADS];
		cam_val[1] = new sc_uint<32>[THREADS];

		reset_cmasks(0);
		reset_cmasks(1);

		for (unsigned int l = 0; l < THREADS; l++) {
			PRF[0][l] = (l == 0);
			PRF[0][THREADS+l] = 1;
		}

		set_vrf_bank_words(4);

		SC_THREAD(thread_wr);
		sensitive << in_clk.pos();

		SC_THREAD(thread_rd);
		sensitive << in_clk.pos();

		SC_THREAD(thread_rd_mask_w);
		sensitive << in_clk.pos();

		SC_THREAD(thread_store);
		sensitive << in_clk_dram.pos();

		SC_THREAD(thread_idx_push);
		sensitive << in_clk_dram.pos();
	}

	/** Destroy register backing storage. */
	~RegFile() {
		delete[] VRF[0];
		delete[] VRF[1];
		delete[] SRF[0];
		delete[] SRF[1];
		delete[] PRF[0];
		delete[] PRF[1];
		delete[] cam_idx[0];
		delete[] cam_idx[1];
		delete[] cam_val[0];
		delete[] cam_val[1];
		delete[] vrf_bank_word_hit_map;

		VRF[0] = nullptr;
		VRF[1] = nullptr;
		SRF[0] = nullptr;
		SRF[1] = nullptr;
		PRF[0] = nullptr;
		PRF[1] = nullptr;
		cam_idx[0] = nullptr;
		cam_idx[1] = nullptr;
		cam_val[0] = nullptr;
		cam_val[1] = nullptr;
		vrf_bank_word_hit_map = nullptr;
	}

	/** Set the RegHazardDetector state class.
	 * @param hd Hazard detector object. */
	void
	setHazardDetector(RegHazardDetect<THREADS,LANES> *hd)
	{
		hazard_detect = hd;
	}

	/** Set the number of 32-bit words in a vector register file bank word.
	 * @param w The number of words in a VRF bank. */
	void
	set_vrf_bank_words(unsigned int w)
	{
		assert(w < (THREADS * 4));

		vrf_bank_words = w;
		alloc_vrf_bank_word_hit_map();

		hazard_detect->set_vrf_bank_words(4);
	}

	/** Debug: Obtain statistics, store them in s
	 * @param s Reference to compute_stats object to write counters to. */
	void
	get_stats(compute_stats &s)
	{
		s.dram_vrf_words_r = dram_vrf_words_r;
		s.dram_vrf_words_w = dram_vrf_words_w;
		s.dram_vrf_net_words_r = dram_vrf_net_words_r;
		s.dram_vrf_net_words_w = dram_vrf_net_words_w;
	}

private:
	/** Allocate the VRF bank word hit map. */
	void
	alloc_vrf_bank_word_hit_map(void)
	{
		if (vrf_bank_word_hit_map) {
			delete[] vrf_bank_word_hit_map;
			vrf_bank_word_hit_map = nullptr;
		}

		vrf_bank_word_hit_map =
				new bool[(THREADS * 4) / vrf_bank_words];
	}

	/** Clear the VRF bank word hit map. */
	void
	clear_vrf_bank_word_hit_map(void)
	{
		unsigned int i;

		for (i = 0; i < (THREADS * 4) / vrf_bank_words; i++)
			vrf_bank_word_hit_map[i] = false;
	}

	/** Return the number of banks accessed.
	 * @return The number of banks accessed. */
	unsigned int
	count_vrf_bank_word_hit_map(void)
	{
		unsigned int i;
		unsigned int c;

		c = 0;

		for (i = 0; i < (THREADS * 4) / vrf_bank_words; i++)
			if (vrf_bank_word_hit_map[i])
				c++;

		return c;
	}

	/** Broadcast a value on all output lanes.
	 * @param value 32-bit value to broadcast.
	 * @param read_port Port to broadcast this value on. */
	void
	broadcast_value(sc_uint<32> value, unsigned int read_port)
	{
		unsigned int l;

		for (l = 0; l < LANES; l++) {
			out_data_r[read_port][l].write(value);
		}
	}

	/** Perform a read from the VRF.
	 * @param reg Register to read.
	 * @param read_port Read port to output this VRF value to. */
	void
	read_vgpr(Register<THREADS/LANES> reg, unsigned int read_port)
	{
		unsigned int l;
		unsigned int offset;

		assert(reg.row < 64);
		assert(reg.col < (THREADS/LANES));

		offset = reg.row * THREADS + reg.col * LANES;

		for (l = 0; l < LANES; l++) {
			out_data_r[read_port][l].write(VRF[reg.wg][offset+l]);
		}

		if (debug_output[DEBUG_COMPUTE_TRACE])
			cout << sc_time_stamp() << " RegFile r " << reg << endl;
	}

	/** Perform a read from the SRF.
	 * @param reg Register to read.
	 * @param read_port Read port to output this VRF value to. */
	void
	read_sgpr(Register<THREADS/LANES> reg, unsigned int read_port)
	{
		assert(reg.row < 32);

		/* Replicate value on all lanes */
		broadcast_value(SRF[reg.wg][reg.row], read_port);

		if (debug_output[DEBUG_COMPUTE_TRACE])
			cout << sc_time_stamp() << " RegFile r " << reg << ": "
				<< SRF[reg.wg][reg.row] << endl;
	}

	/** Perform a read from the PRF.
	 * @param reg Register to read.
	 * @param read_port Read port to output this VRF value to. */
	void
	read_pr(Register<THREADS/LANES> reg, unsigned int read_port)
	{
		unsigned int l;
		unsigned int offset;

		assert(reg.row < 4);
		assert(reg.col < (THREADS/LANES));

		offset = reg.row * THREADS + reg.col * LANES;

		for (l = 0; l < LANES; l++) {
			out_data_r[read_port][l].write(
					PRF[reg.wg][offset+l] ? 1 : 0);
		}

		if (debug_output[DEBUG_COMPUTE_TRACE])
			cout << sc_time_stamp() << " RegFile r " << reg << endl;
	}

	/** Perform a read from special registers.
	 * @param reg Register to read.
	 * @param read_port Read port to output this VRF value to. */
	void
	read_vsp(Register<THREADS/LANES> reg, unsigned int read_port)
	{
		unsigned int l;

		int wg_width;
		unsigned int l_mask;
		unsigned int l_shift;
		unsigned int l_bits;
		unsigned int col;
		unsigned int col_bits;
		unsigned int col_mask;
		unsigned int off = 0;
		unsigned int val;

		assert(reg.row < VSP_SENTINEL);
		assert(reg.col < (THREADS/LANES));

		switch (reg.row) {
		case VSP_ZERO:
			broadcast_value(0, read_port);
			break;
		case VSP_ONE:
			broadcast_value(1, read_port);
			break;
		case VSP_TID_X:
			/* Shift hard-coded to 5, because 32 is minimum line
			 * width. */
			off = in_wg_off[reg.wg][0].read() << 5;
			/* Fall-through */
		case VSP_LID_X:
			wg_width = in_wg_width.read();

			col_bits = max(wg_width - const_log2(LANES/32), 0);
			col = reg.col & ((1 << col_bits) - 1);

			l_mask = (1 << (wg_width + 5)) - 1;

			/* Put the top half together */
			val = off | (col << const_log2(LANES));

			/* Mask in the bottom halves */
			for (l = 0; l < LANES; l++)
				out_data_r[read_port][l].
					write(val | (l & l_mask));
			break;
		case VSP_TID_Y:
			off = in_wg_off[reg.wg][1].read();
			/* fall-through */
		case VSP_LID_Y:
			wg_width = in_wg_width.read();

			/** Column is finnicky, because >> could potentially
			 * rotate. We pre-shift it, such that we can later
			 * unconditionally right-shift without worrying about
			 * overflow.
			 * Example: col = 7, wg_width = 3 (256),
			 * THREADS = 1024, LANES = 128:
			 * col_mask = (8 << 2) - 1       := 0x1f
			 * col_mask -= (1 << 3) - 1      := 0x1f - 0x7 = 0x18
			 * col = 7 << 2 & col_mask	 := 0x1c & 0x18 = 0x18
			 */
			col_mask = ((THREADS/LANES) << const_log2(LANES/32))-1;
			col_mask -= ((1 << wg_width) - 1);
			col = (reg.col << const_log2(LANES/32)) & col_mask;

			l_shift = 5 + wg_width;
			l_bits = max(const_log2(LANES/32) - wg_width, 0);
			l_mask = (1 << l_bits) - 1;

			/* Put the top half together */
			val = off | (col >> wg_width);

			/* Mask in the bottom halves */
			for (l = 0; l < LANES; l++)
				out_data_r[read_port][l].
					write(val | ((l >> l_shift) & l_mask));
			break;
		case VSP_CTRL_BREAK:
		case VSP_CTRL_EXIT:
		case VSP_CTRL_RUN:
		case VSP_CTRL_RET:
			for (l = 0; l < LANES; l++)
				out_data_r[read_port][l].write(
					CMRF[reg.wg][reg.row][reg.col][l] ?
							1 : 0);
			break;
		case VSP_MEM_DATA:
			off = reg.col * LANES;

			for (l = 0; l < LANES; l++)
				out_data_r[read_port][l].write(
						cam_val[reg.wg][off+l]);
			break;
		default:
			assert(false);
			break;
		}

		if (debug_output[DEBUG_COMPUTE_TRACE])
			cout << sc_time_stamp() << " RegFile r " << reg << endl;
	}

	/** Perform a read from scalar special registers.
	 * @param reg Register to read.
	 * @param read_port Read port to output this VRF value to. */
	void
	read_ssp(Register<THREADS/LANES> reg, unsigned int read_port)
	{
		stride_descriptor sd;
		assert(reg.row < SSP_SENTINEL);

		switch (reg.row) {
		case SSP_DIM_X:
		case SSP_DIM_Y:
			broadcast_value(in_dim[reg.row].read(), read_port);
			break;
		case SSP_WG_OFF_X:
			broadcast_value(in_wg_off[reg.wg][0].read() << 5, read_port);
			break;
		case SSP_WG_OFF_Y:
			broadcast_value(in_wg_off[reg.wg][1].read(), read_port);
			break;
		case SSP_WG_WIDTH:
			broadcast_value(32 << in_wg_width.read(), read_port);
			break;
		case SSP_SD_WORDS:
			sd = out_sd[reg.wg].read();
			broadcast_value(sd.words, read_port);
			break;
		case SSP_SD_PERIOD:
			sd = out_sd[reg.wg].read();
			broadcast_value(sd.period, read_port);
			break;
		case SSP_SD_PERIOD_CNT:
			sd = out_sd[reg.wg].read();
			broadcast_value(sd.period_count, read_port);
			break;
		default:
			assert(false);
			break;
		}

		if (debug_output[DEBUG_COMPUTE_TRACE])
			cout << sc_time_stamp() << " RegFile r SSP (" << reg.row << ","
				<< reg.col << ")" << endl;
	}

	/** Perform a write to the VRF.
	 * @param req Requested register.
	 * @param mask Write mask, 1 if write should succeed for this lane. */
	void
	write_vgpr(Register<THREADS/LANES> req, sc_bv<LANES> mask)
	{
		unsigned int offset;
		unsigned int l;
		bfloat r0,r1;

		assert(req.row < 64);
		assert(req.col < (THREADS/LANES));

		/* Anything to write at all? */
		if (mask.or_reduce() == Log_0)
			return;

		offset = req.row * THREADS + req.col * LANES;

		for (l = 0; l < LANES; l++) {
			if (mask.get_bit(l) == Log_1)
				VRF[req.wg][offset+l] = in_data_w[l].read();
		}

		r0.b = VRF[req.wg][offset];
		r1.b = VRF[req.wg][offset+115];

		if (debug_output[DEBUG_COMPUTE_TRACE])
			cout << sc_time_stamp() << " RegFile: w row " << req.row <<
				" col " << req.col << " val[0] " << r0.f <<
				" val[115]" << r1.f << endl;
	}

	/** Perform a write to the SRF.
	 * @param req Requested register. */
	void
	write_sgpr(Register<THREADS/LANES> req)
	{
		assert(req.row < 32);

		SRF[req.wg][req.row] = in_data_w[0].read();

		if (debug_output[DEBUG_COMPUTE_TRACE])
			cout << sc_time_stamp() << " RegFile w SGPR (" <<
				req.row << "): " << SRF[req.wg][req.row] << endl;
	}

	/** Perform a write to the PRF.
	 * @param req Requested register.
	 * @param mask Write mask, 1 if write should succeed for this lane. */
	void
	write_pr(Register<THREADS/LANES> req, sc_bv<LANES> mask)
	{
		unsigned int offset;
		unsigned int l;

		assert(req.row < 4);
		assert(req.col < (THREADS/LANES));

		/* Anything to write at all? */
		if (mask.or_reduce() == Log_0)
			return;

		offset = req.row * THREADS + req.col * LANES;

		for (l = 0; l < LANES; l++, offset++) {
			if (mask.get_bit(l) == Log_1)
				PRF[req.wg][offset] = in_data_w[l].read() & 1;
		}
	}

	/** Perform a write to a Vector Special Purpose register.
	 * @param req Requested register.
	 * @param mask Write mask, 1 if write should succeed for this lane. */
	void
	write_vsp(Register<THREADS/LANES> &req, sc_bv<LANES> mask)
	{
		unsigned int l;
		unsigned int offset;

		assert(req.row < VSP_SENTINEL);
		assert(req.col < (THREADS/LANES));

		/* Anything to write at all? */
		if (mask.or_reduce() == Log_0)
			return;

		for (l = 0; l < LANES; l++) {
			switch (req.row) {
			case VSP_CTRL_RUN:
			case VSP_CTRL_BREAK:
			case VSP_CTRL_RET:
			case VSP_CTRL_EXIT:
				if (mask.get_bit(l) == Log_1)
					CMRF[req.wg][req.row][req.col][l] =
						in_data_w[l].read() ? Log_1 : Log_0;
				break;
			case VSP_MEM_IDX:
				offset = req.col * LANES;

				if (mask.get_bit(l) == Log_1)
					cam_idx[req.wg][offset+l] =
						in_data_w[l].read();
				break;
			case VSP_MEM_DATA:
				offset = req.col * LANES;

				if (mask.get_bit(l) == Log_1)
					cam_val[req.wg][offset+l] =
						in_data_w[l].read();
				break;
			default:
				/* Read-only registers */
				assert(false);
				break;
			}

		}

		if (debug_output[DEBUG_COMPUTE_TRACE])
			cout << sc_time_stamp() << " RegFile: VSP w row " << req.row <<
				" col "	<< req.col << endl;
	}

	/** Perform a write to a Scalar Special Purpose register.
	 * @param reg Register to write.*/
	void
	write_ssp(Register<THREADS/LANES> &reg)
	{
		stride_descriptor sd;
		assert(reg.row < SSP_SENTINEL);

		switch (reg.row) {
		case SSP_SD_WORDS:
			sd = out_sd[reg.wg].read();
			sd.words = in_data_w[0].read();
			out_sd[reg.wg].write(sd);
			break;
		case SSP_SD_PERIOD:
			sd = out_sd[reg.wg].read();
			sd.period = in_data_w[0].read();
			out_sd[reg.wg].write(sd);
			break;
		case SSP_SD_PERIOD_CNT:
			sd = out_sd[reg.wg].read();
			sd.period_count = in_data_w[0].read();
			out_sd[reg.wg].write(sd);
			break;
		default:
			assert(false);
			break;
		}

		if (debug_output[DEBUG_COMPUTE_TRACE])
			cout << sc_time_stamp() << " RegFile r SSP (" << reg.row << ","
				<< reg.col << ")" << endl;
	}

	/** Reset all the cmasks for given workgroup.
	 * @param wg Workgroup to reset execution masks for. */
	void
	reset_cmasks(sc_uint<1> wg)
	{
		unsigned int i, l;
		sc_bv<LANES> mask;

		mask = 0;
		mask.b_not();

		for (i = 0; i < 4; i++)
			for (l = 0; l < THREADS/LANES; l++)
				CMRF[wg][i][l] = mask;
	}

	/** Reset outputs for given workgroup.
	 * @param wg Workgroup to reset outputs for. */
	void
	reset_outputs(sc_uint<1> wg)
	{
		stride_descriptor sd;

		sd.words = 1;
		sd.period = 1;
		sd.period_count = 1;
		out_sd[wg].write(sd);

		thread_active[wg] = Log_1;
		threads_fini[wg] = Log_0;

		out_thread_active.write(thread_active);
		out_wg_finished.write(threads_fini);
	}

	/** Transform an incoming index according to the provided scheme (unit,
	 * 2-vec or 4-vec.
	 * @param t Index transformation scheme
	 * @param idx Index to transform, provided by the DRAM controller.
	 * @return Transformed index as an offset into the vector register
	 * 	   array. */
	sc_uint<32>
	transform_idx(idx_transform_scheme t, sc_uint<32> idx)
	{
		switch (t) {
		case IDX_TRANSFORM_UNIT:
			return idx;
			break;
		case IDX_TRANSFORM_VEC2:
			return (idx & 1) << const_log2(THREADS) |
			       (idx & ~1) >> 1;
			break;
		case IDX_TRANSFORM_VEC4:
			return (idx & 3) << const_log2(THREADS) |
			       (idx & ~3) >> 2;
			break;
		}
	}

	/** Perform a VGPR write coming from the DRAM interface.
	 * @param wg Target work-group slot.
	 * @param row Vector register number (row).
	 * @param mask Write-mask. */
	void
	dram_write_vgpr(sc_uint<1> wg, sc_uint<const_log2(64)> row,
			sc_bv<BUS_WIDTH/4> &mask)
	{
		unsigned int i;
		unsigned int line;
		unsigned int off;
		unsigned int bw;

		reg_offset_t<THREADS> idx[BUS_WIDTH/4];
		sc_bv<BUS_WIDTH/4> conflicts;

		for (i = 0; i < BUS_WIDTH/4; i++)
			idx[i] = in_dram_store_idx[i].read();

		conflicts = hazard_detect->access_vrf_bank_conflict(idx, mask);

		line = (row * THREADS);

		clear_vrf_bank_word_hit_map();

		for (i = 0; i < BUS_WIDTH/4; i++) {
			assert(!conflicts[i]);

			if (!mask[i])
				continue;

			if (!lanes_en[wg][idx[i].lane >> const_log2(LANES)]
					  [idx[i].lane & (LANES-1)])
				continue;

			off = idx[i].row * THREADS + idx[i].lane;
			bw = off / vrf_bank_words;
			off += line;
			VRF[wg][off] = in_dram_store_data[i].read();

			dram_vrf_net_words_w++;
			vrf_bank_word_hit_map[bw] = true;
		}

		dram_vrf_words_w += count_vrf_bank_word_hit_map() *
				vrf_bank_words;
	}

	/** Perform a VGPR read (DRAM write).
	 * @param wg Target work-group slot.
	 * @param row Vector register number (row).
	 * @param mask Read-mask. */
	void
	dram_read_vgpr(sc_uint<1> wg, sc_uint<const_log2(64)> row,
			sc_bv<BUS_WIDTH/4> &mask)
	{
		unsigned int i;
		unsigned int line;
		unsigned int off;
		unsigned int bw;

		reg_offset_t<THREADS> idx[4];
		sc_bv<4> conflicts;
		sc_bv<BUS_WIDTH/4> wmask = 0;

		idx[0] = in_dram_store_idx[0].read();
		idx[1] = in_dram_store_idx[1].read();
		idx[2] = in_dram_store_idx[2].read();
		idx[3] = in_dram_store_idx[3].read();

		conflicts = hazard_detect->access_vrf_bank_conflict(idx, mask);

		line = (row * THREADS);

		clear_vrf_bank_word_hit_map();

		for (i = 0; i < BUS_WIDTH/4; i++) {

			assert(!conflicts[i]);

			if (!mask[i])
				continue;

			/* Only perform this read/DRAM write if this thread is
			 * enabled. */
			if (!lanes_en[wg][idx[i].lane >> const_log2(LANES)]
					  [idx[i].lane & (LANES-1)])
				continue;

			wmask[i] = 1;
			off = idx[i].row * THREADS + idx[i].lane;
			bw = off / vrf_bank_words;
			off += line;
			out_dram_store_data[i].write(VRF[wg][off]);

			dram_vrf_net_words_r++;
			vrf_bank_word_hit_map[bw] = true;
		}

		dram_vrf_words_r += count_vrf_bank_word_hit_map() *
				vrf_bank_words;

		out_dram_store_mask.write(wmask);
	}

	/** Write to CAM registers from DRAM.
	 * @param wg Target work-group slot.
	 * @param row Vector register number (row).
	 * @param mask Write-mask. */
	void
	dram_write_cam(sc_uint<1> wg, sc_uint<const_log2(64)> row,
			sc_bv<BUS_WIDTH/4> &mask)
	{
		unsigned int i;
		unsigned int l;
		reg_offset_t<THREADS> idx;

		assert(row == VSP_MEM_DATA);

		for (i = 0; i < BUS_WIDTH/4; i++) {
			if (!mask[i])
				continue;

			idx = in_dram_store_idx[i].read();

			/** Update each value for which idx corresponds with the
			 * CAM value. IDX being the offset in 32-bit words
			 * inside the buffer. */
			for (l = 0; l < THREADS; l++) {
				if (lanes_en[wg][l >> const_log2(LANES)][l & (LANES-1)] &&
				    cam_idx[wg][l] == idx.idx)
					cam_val[wg][l] = in_dram_store_data[i].read();
			}
		}
	}

	/** Read from CAM registers to DRAM.
	 * @param wg Target work-group slot.
	 * @param row Vector register number (row).
	 * @param mask Read-mask. */
	void
	dram_read_cam(sc_uint<1> wg, sc_uint<const_log2(64)> row,
			sc_bv<BUS_WIDTH/4> &mask)
	{
		unsigned int i;
		unsigned int l;
		reg_offset_t<THREADS> idx;
		sc_bv<BUS_WIDTH/4> wmask = 0;

		assert(row == VSP_MEM_DATA);

		for (i = 0; i < BUS_WIDTH/4; i++) {
			if (!mask[i])
				continue;

			idx = in_dram_store_idx[i].read();

			/* Just perform the first write that matches. */
			for (l = 0; l < THREADS; l++) {
				if (lanes_en[wg][l >> const_log2(LANES)][l & (LANES-1)] &&
				    cam_idx[wg][l] == idx.idx) {
					out_dram_store_data[i].write(cam_val[wg][l]);
					wmask[i] = 1;
					break;
				}
			}
		}

		out_dram_store_mask.write(wmask);
	}

	/** Write from DRAM into consecutive SGPRs.
	 * @param wg Target work-group slot.
	 * @param row Scalar register number (row).
	 * @param mask Write-mask. */
	void
	dram_write_sgpr(sc_uint<1> wg, sc_uint<const_log2(64)> row,
			sc_bv<BUS_WIDTH/4> &mask)
	{
		unsigned int i;
		unsigned int off;
		reg_offset_t<THREADS> idx;

		for (i = 0; i < BUS_WIDTH/4; i++) {
			if (!mask[i])
				continue;

			idx = in_dram_store_idx[i].read();
			off = (row + idx.lane) % 32;

			SRF[wg][off] = in_dram_store_data[i].read();
		}
	}

	/** Read consecutive SGPRs, write to the DRAM interface.
	 * @param wg Target work-group slot.
	 * @param row Scalar register number (row).
	 * @param mask Read-mask. */
	void
	dram_read_sgpr(sc_uint<1> wg, sc_uint<const_log2(64)> row,
			sc_bv<BUS_WIDTH/4> &mask)
	{
		unsigned int i;
		unsigned int off;
		reg_offset_t<THREADS> idx;

		for (i = 0; i < BUS_WIDTH/4; i++) {
			if (!mask[i])
				continue;

			idx = in_dram_store_idx[i].read();
			off = (row + idx.lane) % 32;

			out_dram_store_data[i].write(SRF[wg][off]);
		}
	}

	/** Write data from the scratchpad into a VGPR.
	 * @param wg Target work-group slot.
	 * @param row Vector register number (row).
	 * @param mask Write-mask. */
	void
	sp_write_vgpr(sc_uint<1> wg, sc_uint<const_log2(64)> row,
			sc_bv<BUS_WIDTH_SP> &mask)
	{
		unsigned int i;
		unsigned int line;
		unsigned int off;
		unsigned int bw;

		reg_offset_t<THREADS> idx[BUS_WIDTH_SP];
		sc_bv<BUS_WIDTH_SP> conflicts;

		for (i = 0; i < BUS_WIDTH_SP; i++)
			idx[i] = in_sp_store_idx[wg][i].read();

		conflicts = hazard_detect->access_vrf_bank_conflict(idx, mask);

		line = (row * THREADS);

		clear_vrf_bank_word_hit_map();

		for (i = 0; i < BUS_WIDTH_SP; i++) {
			assert(!conflicts[i]);

			if (!mask[i])
				continue;

			if (!lanes_en[wg][idx[i].lane >> const_log2(LANES)]
					  [idx[i].lane & (LANES-1)])
				continue;

			off = idx[i].row * THREADS + idx[i].lane;
			bw = off / vrf_bank_words;
			off += line;
			VRF[wg][off] = in_sp_store_data[wg][i].read();

			dram_vrf_net_words_w++;
			vrf_bank_word_hit_map[bw] = true;
		}

		dram_vrf_words_w += count_vrf_bank_word_hit_map() *
				vrf_bank_words;
	}

	/** Perform a VGPR read (scratchpad write).
	 * @param wg Target work-group slot.
	 * @param row Vector register number (row).
	 * @param mask Read-mask. */
	void
	sp_read_vgpr(sc_uint<1> wg, sc_uint<const_log2(64)> row,
			sc_bv<BUS_WIDTH_SP> &mask)
	{
		unsigned int i;
		unsigned int line;
		unsigned int off;
		unsigned int bw;

		reg_offset_t<THREADS> idx[BUS_WIDTH_SP];
		sc_bv<BUS_WIDTH_SP> conflicts;
		sc_bv<BUS_WIDTH_SP> wmask = 0;

		for (i = 0; i < BUS_WIDTH_SP; i++)
			idx[i] = in_sp_store_idx[wg][i].read();

		conflicts = hazard_detect->access_vrf_bank_conflict(idx, mask);

		line = (row * THREADS);

		clear_vrf_bank_word_hit_map();

		for (i = 0; i < BUS_WIDTH_SP; i++) {
			assert(!conflicts[i]);

			if (!mask[i])
				continue;

			/* Only perform this read/DRAM write if this thread is
			 * enabled. */
			if (!lanes_en[wg][idx[i].lane >> const_log2(LANES)]
					  [idx[i].lane & (LANES-1)])
				continue;

			wmask[i] = 1;
			off = idx[i].row * THREADS + idx[i].lane;
			bw = off / vrf_bank_words;
			off += line;
			out_sp_store_data[wg][i].write(VRF[wg][off]);

			dram_vrf_net_words_r++;
			vrf_bank_word_hit_map[bw] = true;
		}

		dram_vrf_words_r += count_vrf_bank_word_hit_map() *
				vrf_bank_words;

		out_sp_store_mask[wg].write(wmask);
	}

	/** Write to CAM registers from SP.
	 * @param wg Target work-group slot.
	 * @param row Vector register number (row).
	 * @param mask Write-mask. */
	void
	sp_write_cam(sc_uint<1> wg, sc_uint<const_log2(64)> row,
			sc_bv<BUS_WIDTH_SP> &mask)
	{
		unsigned int i;
		unsigned int l;
		reg_offset_t<THREADS> idx;

		assert(row == VSP_MEM_DATA);

		for (i = 0; i < BUS_WIDTH_SP; i++) {
			if (!mask[i])
				continue;

			idx = in_sp_store_idx[wg][i].read();

			/** Update each value for which idx corresponds with the
			 * CAM value. IDX being the offset in 32-bit words
			 * inside the buffer. */
			for (l = 0; l < THREADS; l++) {
				if (lanes_en[wg][l >> const_log2(LANES)][l & (LANES-1)] &&
				    cam_idx[wg][l] == idx.idx) {
					//cout << l << " " << in_dram_data_w[i].read() << endl;
					cam_val[wg][l] = in_sp_store_data[wg][i].read();
				}
			}
		}
	}

	/** Read from CAM registers into SP.
	 * @param wg Target work-group slot.
	 * @param row Vector register number (row).
	 * @param mask Read-mask. */
	void
	sp_read_cam(sc_uint<1> wg, sc_uint<const_log2(64)> row,
			sc_bv<BUS_WIDTH_SP> &mask)
	{
		unsigned int i;
		unsigned int l;
		reg_offset_t<THREADS> idx;
		sc_bv<BUS_WIDTH_SP> wmask = 0;

		assert(row == VSP_MEM_DATA);

		for (i = 0; i < BUS_WIDTH_SP; i++) {
			if (!mask[i])
				continue;

			idx = in_sp_store_idx[wg][i].read();

			/* Just perform the first write that matches. */
			for (l = 0; l < THREADS; l++) {
				if (lanes_en[wg][l >> const_log2(LANES)][l & (LANES-1)] &&
				    cam_idx[wg][l] == idx.idx) {
					out_sp_store_data[wg][i].write(cam_val[wg][l]);
					wmask[i] = 1;
					break;
				}
			}
		}

		out_sp_store_mask[wg].write(wmask);
	}

	/** Write from SP into consecutive SGPRs.
	 * @param wg Target work-group slot.
	 * @param row Base scalar register number (row).
	 * @param mask Write-mask. */
	void
	sp_write_sgpr(sc_uint<1> wg, sc_uint<const_log2(64)> row,
			sc_bv<BUS_WIDTH_SP> &mask)
	{
		unsigned int i;
		unsigned int off;
		reg_offset_t<THREADS> idx;

		for (i = 0; i < BUS_WIDTH_SP; i++) {
			if (!mask[i])
				continue;

			idx = in_sp_store_idx[wg][i].read();
			off = (row + idx.lane) % 32;

			SRF[wg][off] = in_sp_store_data[wg][i].read();
		}
	}

	/** Read consecutive SGPRs, write to the SP interface.
	 * @param wg Target work-group slot.
	 * @param row Base scalar register number (row).
	 * @param mask Read-mask. */
	void
	sp_read_sgpr(sc_uint<1> wg,sc_uint<const_log2(64)> row,
			sc_bv<BUS_WIDTH_SP> &mask)
	{
		unsigned int i;
		unsigned int off;
		reg_offset_t<THREADS> idx;

		for (i = 0; i < BUS_WIDTH_SP; i++) {
			if (!mask[i])
				continue;

			idx = in_sp_store_idx[wg][i].read();
			off = (row + idx.lane) % 32;

			out_sp_store_data[wg][i].write(SRF[wg][off]);
		}
	}

	/** Update the thread_active and wg_finished signals if a mask write
	 * is completed.
	 * @param wreq Write request that just completed. */
	void
	update_thread_active(Register<THREADS/LANES> &wreq)
	{
		unsigned int i;
		sc_uint<1> wg;
		sc_uint<const_log2(THREADS/LANES) > last_warp;

		if (!in_w.read() || wreq.type != REGISTER_VSP || wreq.row >= 4)
			return;

		wg = wreq.wg;
		last_warp = in_last_warp[wg].read();
		if (wreq.col < last_warp)
			return;

		thread_active[wg] = Log_0;
		threads_fini[wg] = Log_1;

		for (i = 0; i <= last_warp; i++) {
			if (lanes_en[wg][i].or_reduce() == Log_1)
				thread_active[wg] = Log_1;

			if (CMRF[wg][VSP_CTRL_EXIT][i].or_reduce()
					== Log_1)
				threads_fini[wg] = Log_0;
		}

		/** @todo Timing wise this signal probably comes too
		 * late. FIFO instead such that IExecute(?) can block
		 * a delta-cycle? */
		out_thread_active.write(thread_active);
		out_wg_finished.write(threads_fini);
	}

	/** Main thread for writing. Synchronised to clk */
	void
	thread_wr(void)
	{
		unsigned int i;
		sc_bv<LANES> mask_w;
		Register<THREADS/LANES> wreq;

		sc_bv<LANES> mask_one = 0;
		mask_one.b_not();

		reset_outputs(0);
		reset_outputs(1);

		while (true) {
			wreq = in_req_w.read();

			/* A write */
			if (in_ignore_mask_w.read() || wreq.type == REGISTER_SGPR ||
			    wreq.type == REGISTER_SSP)
				mask_w = mask_one;
			else
				mask_w = in_mask_w.read();

			if (in_w.read() && mask_w.or_reduce() == Log_1) {
				switch (wreq.type) {
				case REGISTER_VGPR:
					write_vgpr(wreq, mask_w);
					break;
				case REGISTER_SGPR:
					write_sgpr(wreq);
					break;
				case REGISTER_PR:
					write_pr(wreq, mask_w);
					break;
				case REGISTER_VSP:
					write_vsp(wreq, mask_w);
					break;
				case REGISTER_SSP:
					write_ssp(wreq);
					break;
				default:
					assert(false);
				}
			}

			/* If requested, reset the CMASK for a WG.
			 * @todo Collisions with other operation? */
			if (in_cmask_rst.read()) {
				reset_cmasks(in_cmask_rst_wg.read());
				reset_outputs(in_cmask_rst_wg.read());
			}

			/* Determine post-write whether there is at least one
			 * thread active. If not, the control stack needs to
			 * be popped.
			 *
			 * By placing this code after the write, we get the
			 * new values back for this cycle. If latencies cannot
			 * be met, this still emulates a write-forwarding
			 * technique.*/
			for (i = 0; i < THREADS/LANES; i++) {
				lanes_en[0][i] = CMRF[0][VSP_CTRL_RUN][i] &
						 CMRF[0][VSP_CTRL_BREAK][i] &
						 CMRF[0][VSP_CTRL_RET][i] &
						 CMRF[0][VSP_CTRL_EXIT][i];

				lanes_en[1][i] = CMRF[1][VSP_CTRL_RUN][i] &
						 CMRF[1][VSP_CTRL_BREAK][i] &
						 CMRF[1][VSP_CTRL_RET][i] &
						 CMRF[1][VSP_CTRL_EXIT][i];
			}

			update_thread_active(wreq);

			wait();
		}
	}

	/** Main thread for reading. Performs max one request (3 (v)regs) per
	 * cycle.
	 * Timing-wise. This will run as soon as the inputs change. In essence
	 * this means that when IDecode finishes generating signals, this
	 * will start running. Achieved by using a blocking request fifo of
	 * depth 1.
	 * In this cycle, the output should be changed last, that is: after
	 * IExecute read the values from the previous request, but at the
	 * latest first thing of the next cycle. This should be ensured by
	 * the SystemC simulation model, where */
	void
	thread_rd(void)
	{
		unsigned int p;
		reg_read_req<THREADS/LANES> req;
		sc_bv<3> conflicts;
		RequestTarget dst;

		while (true) {
			req = in_req_r.read();
			/* Performs writes in the next delta cycle, ensures
			 * IExecute finished */
			wait(SC_ZERO_TIME);

			conflicts = hazard_detect->execute_bank_conflict(req);
			dst = in_dram_dst.read();

			/* And three reads.
			 *
			 * Do not perform a write on ports if the req.r[] mask
			 * is Log_0. Crucial for conflict resolution!
			 * @todo Do we need three channels for all or just VGPR?
			 * Seems only crucial for MAD. */
			for (p = 0; p < 3; p++) {
				if (!req.r[p] || conflicts[p])
					continue;

				assert(!in_store_enable[IF_DRAM].read() ||
					(dst.type != TARGET_CAM && dst.type != TARGET_REG) ||
					!hazard_detect->ae_hazard(in_store_reg[IF_DRAM].read(), req.reg[p]));
				assert(!in_store_enable[req.reg[p].wg].read() || !hazard_detect->
					ae_hazard(in_store_reg[req.reg[p].wg].read(), req.reg[p]));

				switch (req.reg[p].type) {
				case REGISTER_VGPR:
					read_vgpr(req.reg[p], p);
					break;
				case REGISTER_SGPR:
					read_sgpr(req.reg[p], p);
					break;
				case REGISTER_PR:
					read_pr(req.reg[p], p);
					break;
				case REGISTER_VSP:
					read_vsp(req.reg[p], p);
					break;
				case REGISTER_SSP:
					read_ssp(req.reg[p], p);
					break;
				case REGISTER_IMM:
					broadcast_value(req.imm[p],p);
					break;
				default:
					assert(false);
				}
			}

			out_req_conflicts.write(conflicts);

			/* Don't process another until next clock cycle */
			wait();
		}
	}

	/** Separate read channel for the active thread mask.
	 * This mask is defined as the AND product of the four masks, which
	 * is calculated in thread_wr(). */
	void
	thread_rd_mask_w(void)
	{
		sc_uint<const_log2(THREADS/LANES)> col;
		unsigned int wg;

		while (true) {
			col = in_col_mask_w.read();
			wg = in_wg_mask_w.read();

			if (debug_output[DEBUG_COMPUTE_TRACE])
				cout << sc_time_stamp() << " RegFile cmask col " << col << endl;
			/* Performs writes in the next delta cycle, ensures
			 * IExecute finished */
			wait(SC_ZERO_TIME);

			out_mask_w.write(lanes_en[wg][col]);

			/* Don't process another until next clock cycle */
			wait();
		}
	}

	/** Perform a read/write operation instructed by DRAM. */
	void
	do_store_dram(void)
	{
		sc_bv<BUS_WIDTH/4> mask;
		AbstractRegister reg;

		mask = in_dram_store_mask.read();
		if (mask.or_reduce() == Log_0)
			return;

		reg = in_store_reg[IF_DRAM].read();

		switch (reg.type) {
		case REGISTER_VGPR:
			if (in_store_write[IF_DRAM].read())
				dram_write_vgpr(reg.wg, reg.row, mask);
			else
				dram_read_vgpr(reg.wg, reg.row, mask);
			break;
		case REGISTER_SGPR:
			if (in_store_write[IF_DRAM].read())
				dram_write_sgpr(reg.wg, reg.row, mask);
			else
				dram_read_sgpr(reg.wg, reg.row, mask);
			break;
		case REGISTER_VSP:
			if (in_store_write[IF_DRAM].read())
				dram_write_cam(reg.wg, reg.row, mask);
			else
				dram_read_cam(reg.wg, reg.row, mask);
			break;
		default:
			cerr << "Unknown store from DRAM interface." << endl;
			assert(false);
			break;
		}
	}

	/** perform a read/write operation instruction by the scratchpad.
	 * @param intf Scratchpad interface (work-group) */
	void
	do_store_sp(req_if_t intf)
	{
		sc_bv<BUS_WIDTH_SP> mask;
		AbstractRegister reg;

		mask = in_sp_store_mask[intf].read();
		if (mask.or_reduce() == Log_0)
			return;

		reg = in_store_reg[intf].read();

		switch (reg.type) {
		case REGISTER_VGPR:
			if (in_store_write[intf].read())
				sp_write_vgpr(reg.wg, reg.row, mask);
			else
				sp_read_vgpr(reg.wg, reg.row, mask);
			break;
		case REGISTER_SGPR:
			if (in_store_write[intf].read())
				sp_write_sgpr(reg.wg, reg.row, mask);
			else
				sp_read_sgpr(reg.wg, reg.row, mask);
			break;
		case REGISTER_VSP:
			if (in_store_write[intf].read())
				sp_write_cam(reg.wg, reg.row, mask);
			else
				sp_read_cam(reg.wg, reg.row, mask);
			break;
		default:
			cerr << "Unknown SP store from interface " << intf << endl;
			assert(false);
			break;
		}
	}

	/** Storage back-end read/write port thread. */
	void
	thread_store(void)
	{
		sc_bv<BUS_WIDTH/4> mask;
		AbstractRegister reg;
		RequestTarget dst;

		while (true) {
			wait();

			dst = in_dram_dst.read();

			if (in_store_enable[IF_DRAM].read() &&
			   (dst.type == TARGET_REG || dst.type == TARGET_CAM)) {
				assert(!in_store_enable[dst.wg.to_int()].read());

				do_store_dram();
			}

			if (in_store_enable[IF_SP_WG0].read())
				do_store_sp(IF_SP_WG0);

			if (in_store_enable[IF_SP_WG1].read())
				do_store_sp(IF_SP_WG1);
		}
	}

	/** The thread that pushes CAM indexes to the index iterator FIFOs. */
	void
	thread_idx_push(void)
	{
		unsigned int l;
		AbstractRegister reg;
		enum {
			STATE_IDLE,
			STATE_PUSH,
		} state = STATE_IDLE;

		while(true) {
			wait();

			switch (state) {
			case STATE_IDLE:
				if (!in_store_idx_push_trigger.read())
					break;

				reg = in_store_reg[IF_DRAM].read();

				l = 0;
				state = STATE_PUSH;
				/* fall-through */
			case STATE_PUSH:
				if (l < THREADS) {
					if (lanes_en[reg.wg][l >> const_log2(LANES)][l & (LANES-1)])
						out_store_idx.write(idx_t<THREADS>(l,cam_idx[reg.wg][l]));

					l++;
				} else {
					/** Dummy to indicate this was the last. */
					out_store_idx.write(idx_t<THREADS>());
					state = STATE_IDLE;
				}
				break;
			}

		}
	}
};

}

#endif /* COMPUTE_CONTROL_REGFILE_1S_3R1W_H */
