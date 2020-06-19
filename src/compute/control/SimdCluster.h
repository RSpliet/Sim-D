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

#ifndef COMPUTE_CONTROL_SIMDCLUSTER_H
#define COMPUTE_CONTROL_SIMDCLUSTER_H

#include <string>
#include <systemc>

#include "util/defaults.h"
#include "util/sched_opts.h"
#include "model/Buffer.h"
#include "model/request_target.h"

#include "compute/control/IFetch.h"
#include "compute/control/IDecode_1S.h"
#include "compute/control/IDecode_3S.h"
#include "compute/control/IExecute.h"
#include "compute/control/IMem.h"
#include "compute/control/CtrlStack.h"
#include "compute/control/RegFile.h"
#include "compute/control/Scoreboard.h"
#include "compute/control/BufferToPhysXlat.h"
#include "compute/control/RegHazardDetect_1R1W_16b.h"
#include "sp/control/Scratchpad.h"

using namespace sc_dt;
using namespace sc_core;
using namespace compute_model;
using namespace simd_model;
using namespace compute_control;
using namespace sp_control;
using namespace std;

namespace compute_control {

/** Single SimdCluster module.
 *
 * Instantiates all relevant submodules and provides a simpler coherent
 * interface. Maintains the state of both work-groups in this SimdCluster,
 * pulling work-groups from the WorkScheduler whenever idle.
 *
 * @param THREADS 	Number of threads in a work-group.
 * @param LANES 	Number of FPUs in this SimdCluster.
 * @param RCPUS 	Number of floating point ReCiProcal div/sqrt Units in
 * 			this SimdCluster.
 * @param PC_WIDTH 	Number of bits reserved for the program counter. log2 of
 * 			IMEM size.
 * @param XLAT_ENTRIES 	Number of mapped DRAM buffers supported.
 * @param BUS_WIDTH 	Number of words transferred in a single DRAM burst.
 * @param BUS_WIDTH_SP  Number of words transferred in a scratchpad cycle.
 */
template <unsigned int THREADS, unsigned int LANES, unsigned int RCPUS,
	unsigned int PC_WIDTH, unsigned int XLAT_ENTRIES,
	unsigned int BUS_WIDTH, unsigned int BUS_WIDTH_SP>
class SimdCluster : public sc_module
{
public:
	/** Compute clock */
	sc_in<bool> in_clk{"in_clk"};

	/** DRAM clock */
	sc_in<bool> in_clk_dram{"in_clk_dram"};

	/** Synchronous reset signal */
	sc_in<bool> in_rst{"in_rst"};

	/** Workgroup fifo, incoming from the workscheduler */
	sc_fifo_in<workgroup<THREADS,LANES> > in_wg{"in_wg"};

	/** Dimensions for currently active program */
	sc_in<sc_uint<32> > in_work_dim[2];

	/** Width of workgroup of currently active program */
	sc_in<workgroup_width> in_wg_width{"in_wg_width"};

	/** Scheduling options. */
	sc_in<sc_bv<WSS_SENTINEL> > in_sched_opts{"in_sched_opts"};

	/** Ticket number for next stride_descriptor ready to pop off
	 * DRAM/SPs FIFOs. */
	sc_inout<sc_uint<4> > out_ticket_pop{"out_ticket_pop"};

	/****************** Direct pass-through to IMem ******************/
	/** Program upload interface from workscheduler, operand. */
	sc_in<Instruction> in_prog_op_w[4];
	/** Progrma upload interface from workscheduler, PC. */
	sc_in<sc_uint<11> > in_prog_pc_w{"in_prog_pc_w"};
	/** Program upload interface from workscheduler, enable */
	sc_in<bool> in_prog_w{"in_prog_w"};

	/** WorkScheduler issued last workgroup */
	sc_in<bool> in_end_prg{"in_end_prg"};

	/** Finished execution of all workgroups in flight */
	sc_inout<bool> out_exec_fini{"out_exec_fini"};

	/************ Direct pass-through to BufferToPhysXlat ************/
	/** Write a translation table entry */
	sc_in<bool> in_xlat_w{"in_xlat_w"};

	/** Buffer index to write to. */
	sc_in<sc_uint<const_log2(XLAT_ENTRIES)> > in_xlat_idx_w{"in_xlat_idx_w"};

	/** Physical address indexed by buffer index. */
	sc_in<Buffer> in_xlat_phys_w{"in_xlat_phys_w"};

	/************ Direct pass-through to BufferToPhysXlat ************/
	/** Write a translation table entry */
	sc_in<bool> in_sp_xlat_w{"in_sp_xlat_w"};

	/** Buffer index to write to. */
	sc_in<sc_uint<const_log2(XLAT_ENTRIES)> > in_sp_xlat_idx_w{"in_sp_xlat_idx_w"};

	/** Physical address indexed by buffer index. */
	sc_in<Buffer> in_sp_xlat_phys_w{"in_sp_xlat_phys_w"};

	/*************** Pass-through to the memory controller ************/
	/** Data path is enabled. */
	sc_in<bool> in_dram_enable{"in_dram_enable"};

	/** True iff this DRAM operation writes to the register file, false if
	 * it reads from the register file. */
	sc_in<bool> in_dram_write{"in_dram_write"};

	/** Destination (Register file/SP, WG) for active DRAM request. */
	sc_in<RequestTarget> in_dram_dst{"in_dram_dst"};

	/** DRAM requests. */
	sc_fifo_out<stride_descriptor> out_desc_fifo{"out_desc_fifo"};

	/** Kick off DRAM request */
	sc_fifo_out<bool> out_dram_kick{"out_dram_kick"};

	/** MC signals whether it's done processing a DRAM request for given
	 * destination.
	 *
	 * Using a FIFO to easily cross clock domains in SystemC. */
	sc_fifo_in<RequestTarget> in_dram_done_dst{"in_dram_done_dst"};

	/** DRAM write mask */
	sc_in<sc_bv<BUS_WIDTH/4> > in_dram_mask{"in_dram_mask"};

	/** Data to write back to register */
	sc_in<sc_uint<32> > in_dram_data[BUS_WIDTH/4];

	/** Data to read from register */
	sc_inout<sc_uint<32> > out_dram_data[IF_SENTINEL][BUS_WIDTH/4];

	/** Refresh in progress. */
	sc_in<bool> in_dram_ref{"in_dram_ref"};

	/************ Write path to Register file ****************/
	/** Index within register to read/write to */
	sc_in<reg_offset_t<THREADS> > in_dram_idx[4];

	/** Register addressed by DRAM */
	sc_in<AbstractRegister> in_dram_reg{"in_dram_reg"};

	/** Write mask taking into account individual lane status */
	sc_inout<sc_bv<BUS_WIDTH/4> > out_dram_mask;

	/** Triggers pushing the DRAM indexes from the vc.cam_idx register
	 * to the DRAM controller's index iterator. */
	sc_in<bool> in_dram_idx_push_trigger{"in_dram_idx_push_trigger"};

	/** Indexes to read for index iterative transfer, one per cycle. */
	sc_fifo_out<idx_t<THREADS> > out_dram_idx{"out_dram_idx"};

	/************ Write path to scratchpads *****************/
	/** Base scratchpad address that DRAM tries to write to. */
	sc_in<sc_uint<18> > in_dram_sp_addr{"in_dram_sp_addr"};

private:
	/** Boolean indicating whether elaborate() has been called. */
	bool elaborated;

	/** Chosen IDecode implementation (1- vs. 3-stage). */
	IDecode_impl idec_impl;

	/****************** Signals I must generate ******************/
	/** Workgroup global thread ID offsets */
	sc_signal<sc_uint<32> > simdcluster_wg_off[2][2];

	/** Last warp for each workgroup */
	sc_signal<sc_uint<const_log2(THREADS/LANES)> > simdcluster_last_warp[2];

	/** Workgroup is active */
	sc_signal<workgroup_state> simdcluster_wg_state[2];

	/** Workgroup to reset CMask/PC for. */
	sc_signal<sc_uint<1> > simdcluster_rst_wg;

	/** Reset the cmask and PC */
	sc_signal<bool> simdcluster_rst;

	/* Local variables */
	/** State of each work-group. */
	workgroup_state wg_state[2];

	/** Boolean indicated whether work-group slot is ready to be filled. */
	bool wg_accept_next[2];

	/** Perf counter: Number of active DRAM cycles. */
	unsigned long dram_active;
	/** Perf counter: Number of active compute cycles. */
	unsigned long compute_active;
	/** Perf counter: Number of active SP cycles. */
	unsigned long sp_active[2];

	/** Counter indicating which stride_descriptor should be popped next.
	 *
	 * @todo No longer feasible in a set-up with multiple SimdClusters. */
	sc_uint<4> ticket_pop;

	/** @cond Doxygen_Suppress */
	/****************** Child modules ******************/
	IFetch<PC_WIDTH> ifetch;
	IDecode<PC_WIDTH,THREADS,LANES,RCPUS,XLAT_ENTRIES> *idecode;
	IExecute<PC_WIDTH,THREADS,LANES,RCPUS> iexecute;
	IMem<PC_WIDTH> imem;
	RegFile<THREADS,LANES,BUS_WIDTH,BUS_WIDTH_SP> regfile;
	CtrlStack<THREADS,LANES,PC_WIDTH,COMPUTE_CSTACK_ENTRIES> ctrlstack;
	Scoreboard<THREADS,LANES> scoreboard;
	BufferToPhysXlat<XLAT_ENTRIES> xlat;
	BufferToPhysXlat<XLAT_ENTRIES> xlat_sp;
	Scratchpad<0,BUS_WIDTH/4,BUS_WIDTH_SP,SP_BYTES,THREADS> sp_0;
	Scratchpad<1,BUS_WIDTH/4,BUS_WIDTH_SP,SP_BYTES,THREADS> sp_1;

	/****************** Child module wiring ******************/
	/* IFetch -> IMem */
	sc_fifo<imem_request<PC_WIDTH> > ifetch_insn_r;

	/* IFetch -> IDecode */
	sc_signal<sc_uint<1> > ifetch_wg;

	/* IMem -> IDecode */
	sc_signal<Instruction> imem_op;
	sc_signal<sc_uint<PC_WIDTH> > imem_pc;

	/* IDecode -> RegFile */
	sc_signal<sc_bv<3> > idecode_r;
	sc_fifo<reg_read_req<THREADS/LANES> > idecode_req_r;
	sc_signal<sc_uint<const_log2(THREADS/LANES)> > idecode_col_r;

	/* IDecode -> IExecute */
	sc_signal<Instruction> idecode_insn;
	sc_signal<sc_uint<PC_WIDTH> > idecode_pc;
	sc_signal<sc_uint<const_log2(THREADS/LANES)> > idecode_col_w;
	sc_signal<sc_uint<const_log2(LANES/RCPUS)> > idecode_subcol_w;
	sc_signal<sc_uint<1> > idecode_wg;

	/* IDecode -> IFetch */
	sc_signal<bool> idecode_stall_f;

	/* IDecode -> Scoreboard */
	sc_signal<bool> idecode_enqueue;
	sc_signal<bool> idecode_enqueue_cstack_write;
	sc_signal<sc_uint<1> > idecode_enqueue_cstack_wg;
	sc_signal<Register<THREADS/LANES> > idecode_req_w;
	sc_fifo<reg_read_req<THREADS/LANES> > idecode_req_r_sb;
	sc_signal<sc_bv<32> > idecode_req_sb_pop[3];
	sc_signal<bool> idecode_ssp_match;

	/* IDecode -> BufferToPhysXlat */
	sc_signal<sc_uint<const_log2(XLAT_ENTRIES)> > idecode_xlat_idx;
	sc_signal<sc_uint<const_log2(XLAT_ENTRIES)> > idecode_sp_xlat_idx;

	/* RegFile -> IExecute */
	sc_signal<sc_uint<32> > regfile_data_r[3][LANES];
	sc_signal<stride_descriptor> regfile_sd[2];

	/* IDecode -> IExecute */
	sc_signal<sc_uint<32> > idecode_data_r[2][LANES];

	/* RegFile -> RegFile */
	sc_signal<sc_bv<LANES> > regfile_mask_w;

	/* RegFile -> IDecode */
	sc_fifo<sc_bv<3> > regfile_req_conflicts;

	/* RegFile -> XXX */
	sc_signal<sc_bv<2> > regfile_thread_active;
	sc_signal<sc_bv<2> > regfile_wg_finished;
	sc_signal<sc_uint<32> > regfile_store_data[2][BUS_WIDTH_SP];
	sc_signal<sc_bv<BUS_WIDTH_SP> > regfile_store_mask[2];

	/* IExecute -> RegFile */
	sc_signal<sc_uint<32> > iexecute_data_w[LANES];
	sc_signal<Register<THREADS/LANES> > iexecute_req_w;
	sc_signal<sc_uint<1> > iexecute_wg_w;
	sc_signal<bool> iexecute_w;
	sc_fifo<sc_uint<const_log2(THREADS/LANES)> > iexecute_col_mask_w;
	sc_signal<bool> iexecute_ignore_mask_w;

	/* IExecute -> Scoreboard */
	sc_signal<bool> iexecute_dequeue_sb;
	sc_signal<bool> iexecute_dequeue_sb_cstack_write;

	/* IExecute -> CStack */
	sc_signal<ctrlstack_action> iexecute_cstack_action;
	sc_signal<ctrlstack_entry<THREADS,PC_WIDTH> > iexecute_cstack_entry;

	/* IExecute -> SimdCluster */
	sc_signal<workgroup_state> iexecute_wg_state_next[2];
	sc_signal<sc_bv<2> > iexecute_wg_exit_commit;

	/* CStack -> IExecute */
	sc_signal<bool> cstack_ex_overflow;
	sc_signal<ctrlstack_entry<THREADS,PC_WIDTH> > cstack_top;
	sc_signal<bool> cstack_full;
	sc_signal<sc_uint<const_log2(COMPUTE_CSTACK_ENTRIES) + 1> > cstack_sp;

	/* IExecute -> IFetch */
	sc_signal<bool> iexecute_pc_do_w;
	sc_signal<sc_uint<PC_WIDTH> > iexecute_pc_w;

	sc_signal<workgroup_state> iexecute_wg_next_state[2];

	/* IExecute -> Scratchpad */
	sc_fifo<stride_descriptor> iexecute_sp_desc_fifo_0;
	sc_fifo<stride_descriptor> iexecute_sp_desc_fifo_1;
	sc_fifo<bool> iexecute_store_kick_0;
	sc_fifo<bool> iexecute_store_kick_1;

	/* Scratchpad -> ??? */
	sc_fifo<sc_uint<1> > sp_wg_done_0;
	sc_fifo<sc_uint<1> > sp_wg_done_1;

	/* Scratchpad -> RegFile */
#if SP_BUS_WIDTH != (MC_BUS_WIDTH/4)
	sc_signal<sc_uint<32> > sp_out_data[2][BUS_WIDTH_SP-(BUS_WIDTH/4)];
#endif
	sc_signal<bool> sp_rf_enable[2];
	sc_signal<bool> sp_rf_write[2];
	sc_signal<AbstractRegister> sp_rf_reg[2];
	sc_signal<sc_bv<BUS_WIDTH_SP> > sp_rf_mask[2];
	sc_signal<reg_offset_t<THREADS> > sp_rf_idx[2][BUS_WIDTH_SP];

	/* Scoreboard -> IDecode */
	sc_fifo<sc_bv<3> > scoreboard_raw;
	sc_signal<bool> scoreboard_ex_overflow;
	sc_signal<bool> scoreboard_cpop_stall[2];
	sc_signal<sc_bv<32> > scoreboard_entries_pop[2];

	sc_signal<bool> ifetch_pc_rst;
	sc_signal<sc_uint<1> > ifetch_pc_rst_wg;

	/* BufferToPhysXlat -> IExecute */
	sc_signal<Buffer> xlat_phys;
	sc_signal<Buffer> xlat_sp_phys;
	/** @endcond */

public:
	/** Constructor. */
	SC_CTOR(SimdCluster) :
		elaborated(false), idec_impl(IDECODE_1S), dram_active(0ul),
		compute_active(0ul),
		ifetch("ifetch"), idecode(nullptr), iexecute("iexecute"),
		imem("imem"), regfile("regfile"), ctrlstack("ctrlstack"),
		scoreboard("scoreboard"), xlat("xlat"), xlat_sp("xlat_sp"),
		sp_0("sp_0"), sp_1("sp_1"),
		ifetch_insn_r(1), idecode_req_r(1),
		idecode_req_r_sb(1), regfile_req_conflicts(1),
		iexecute_col_mask_w(1), iexecute_sp_desc_fifo_0(1),
		iexecute_sp_desc_fifo_1(1), iexecute_store_kick_0(2),
		iexecute_store_kick_1(2), sp_wg_done_0(1), sp_wg_done_1(1),
		scoreboard_raw(1)
	{
		sp_active[0] = 0ul;
		sp_active[1] = 0ul;

		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();

		do_reset();
	}

	/** Choose the active IDecode implementation
	 * @param impl The desired IDecode implentation */
	void
	setIDecode(IDecode_impl impl)
	{
		assert(!elaborated);

		idec_impl = impl;

		if (idec_impl == IDECODE_3S)
			setRegHazardDetector(new RegHazardDetect_1R1W_16b
					<COMPUTE_THREADS,COMPUTE_FPUS>());
	}

	/** Set the register file hazard detector.
	 * @param hd Pointer to a RegHazardDetector object. */
	void
	setRegHazardDetector(RegHazardDetect<THREADS,LANES> *hd)
	{
		regfile.setHazardDetector(hd);
	}

	/**
	 * Set the number of pipeline stages for IExecute.
	 *
	 * Adjusts the number of scoreboard slots accordingly.
	 * @param stages Number of IExecute pipeline stages.
	 */
	void
	iexecute_pipeline_stages(unsigned int stages)
	{
		unsigned int dec_stages;

		if (!idecode)
			throw logic_error("Cannot set IExecute pipeline stages "
					"prior to elaboration of design.");

		iexecute.set_pipeline_stages(stages);
		idecode->set_iexec_pipeline_stages(stages);
		dec_stages = idecode->get_pipeline_stages();
		scoreboard.set_slots(stages + dec_stages);
	}

	/** Set VRF bank width.
	 * @param w Number of (32-bit) words to set the VRF bank width to. */
	void
	regfile_set_vrf_bank_words(unsigned int w)
	{
		regfile.set_vrf_bank_words(w);
	}

	/**
	 * Retreive performance counters from SimdCluster and subsystems
	 *
	 * @param s Reference to stats object to store results in. */
	void
	get_stats(compute_stats &s)
	{
		idecode->get_stats(s);
		iexecute.get_stats(s);
		regfile.get_stats(s);

		s.max_scoreboard_entries = scoreboard.get_max_entries();
		s.dram_active = dram_active;
		s.sp_active[0] = sp_active[0];
		s.sp_active[1] = sp_active[1];
		s.compute_active = compute_active;
	}

	/** Wire up the SimdCluster. */
	void
	elaborate(void)
	{
		unsigned int i, l;

		/* IFetch */
		ifetch.in_clk(in_clk);
		ifetch.out_insn_r(ifetch_insn_r);
		ifetch.out_wg(ifetch_wg);
		ifetch.in_wg_state[0](simdcluster_wg_state[0]);
		ifetch.in_wg_state[1](simdcluster_wg_state[1]);
		ifetch.in_wg_finished(regfile_wg_finished);
		ifetch.in_pc_write(iexecute_pc_do_w);
		ifetch.in_pc_w(iexecute_pc_w);
		ifetch.in_pc_wg_w(iexecute_wg_w);
		ifetch.in_stall_d(idecode_stall_f);
		ifetch.in_pc_rst(simdcluster_rst);
		ifetch.in_pc_rst_wg(simdcluster_rst_wg);
		ifetch.in_sched_opts(in_sched_opts);

		/* IMem */
		imem.in_clk(in_clk);
		imem.in_insn_r(ifetch_insn_r);
		imem.out_op(imem_op);
		imem.out_pc(imem_pc);
		for (i = 0; i < 4; i++)
			imem.in_op_w[i](in_prog_op_w[i]);
		imem.in_pc_w(in_prog_pc_w);
		imem.in_w(in_prog_w);

		switch (idec_impl) {
		case IDECODE_1S:
			elaborate_idecode_1s();
			break;
		case IDECODE_3S:
			elaborate_idecode_3s();
			break;
		default:
			assert(false);
			break;
		}

		/* Regfile */
		regfile.in_clk(in_clk);
		regfile.in_clk_dram(in_clk_dram);
		regfile.in_req_r(idecode_req_r);
		for (i = 0; i < 3; i++) {
			for (l = 0; l < LANES; l++)
				regfile.out_data_r[i][l](regfile_data_r[i][l]);
		}

		regfile.out_req_conflicts(regfile_req_conflicts);
		regfile.in_req_w(iexecute_req_w);
		for (l = 0; l < LANES; l++)
			regfile.in_data_w[l](iexecute_data_w[l]);

		regfile.in_mask_w(regfile_mask_w);
		regfile.in_w(iexecute_w);
		regfile.in_last_warp[0](simdcluster_last_warp[0]);
		regfile.in_last_warp[1](simdcluster_last_warp[1]);
		regfile.in_wg_mask_w(iexecute_wg_w);
		regfile.in_col_mask_w(iexecute_col_mask_w);
		regfile.out_mask_w(regfile_mask_w);
		regfile.in_ignore_mask_w(iexecute_ignore_mask_w);
		regfile.out_thread_active(regfile_thread_active);
		regfile.out_wg_finished(regfile_wg_finished);

		regfile.in_cmask_rst(simdcluster_rst);
		regfile.in_cmask_rst_wg(simdcluster_rst_wg);
		for (i = 0; i < 2; i++) {
			for (l = 0; l < 2; l++) {
				regfile.in_wg_off[i][l](simdcluster_wg_off[i][l]);
				iexecute.in_wg_off[i][l](simdcluster_wg_off[i][l]);

			}
			regfile.in_dim[i](in_work_dim[i]);
			iexecute.in_dim[i](in_work_dim[i]);
		}
		regfile.in_wg_width(in_wg_width);

		regfile.in_store_enable[IF_DRAM](in_dram_enable);
		regfile.in_store_enable[IF_SP_WG0](sp_rf_enable[IF_SP_WG0]);
		regfile.in_store_enable[IF_SP_WG1](sp_rf_enable[IF_SP_WG1]);

		regfile.in_store_write[IF_DRAM](in_dram_write);
		regfile.in_store_write[IF_SP_WG0](sp_rf_write[IF_SP_WG0]);
		regfile.in_store_write[IF_SP_WG1](sp_rf_write[IF_SP_WG1]);

		regfile.in_store_reg[IF_DRAM](in_dram_reg);
		regfile.in_store_reg[IF_SP_WG0](sp_rf_reg[IF_SP_WG0]);
		regfile.in_store_reg[IF_SP_WG1](sp_rf_reg[IF_SP_WG1]);

		regfile.in_dram_store_mask(in_dram_mask);
		regfile.in_sp_store_mask[IF_SP_WG0](sp_rf_mask[IF_SP_WG0]);
		regfile.in_sp_store_mask[IF_SP_WG1](sp_rf_mask[IF_SP_WG1]);

		for (unsigned int i = 0; i < BUS_WIDTH/4; i++) {
			regfile.in_dram_store_idx[i](in_dram_idx[i]);
			regfile.in_dram_store_data[i](in_dram_data[i]);
			regfile.out_dram_store_data[i](out_dram_data[IF_DRAM][i]);

			regfile.in_sp_store_data[IF_SP_WG0][i](out_dram_data[IF_SP_WG0][i]);
			regfile.in_sp_store_data[IF_SP_WG1][i](out_dram_data[IF_SP_WG1][i]);
		}

		for (unsigned int i = 0; i < BUS_WIDTH_SP; i++) {
			regfile.in_sp_store_idx[IF_SP_WG0][i](sp_rf_idx[IF_SP_WG0][i]);
			regfile.in_sp_store_idx[IF_SP_WG1][i](sp_rf_idx[IF_SP_WG1][i]);

			regfile.out_sp_store_data[IF_SP_WG0][i](regfile_store_data[IF_SP_WG0][i]);
			regfile.out_sp_store_data[IF_SP_WG1][i](regfile_store_data[IF_SP_WG1][i]);
		}

#if SP_BUS_WIDTH != (MC_BUS_WIDTH/4)
		for (i = 0; i < SP_BUS_WIDTH - (BUS_WIDTH/4); i++) {
			regfile.in_sp_store_data[IF_SP_WG0][(BUS_WIDTH/4)+i](sp_out_data[IF_SP_WG0][i]);
			regfile.in_sp_store_data[IF_SP_WG1][(BUS_WIDTH/4)+i](sp_out_data[IF_SP_WG1][i]);
		}
#endif

		regfile.in_dram_dst(in_dram_dst);
		regfile.out_dram_store_mask(out_dram_mask);
		regfile.out_sp_store_mask[IF_SP_WG0](regfile_store_mask[IF_SP_WG0]);
		regfile.out_sp_store_mask[IF_SP_WG1](regfile_store_mask[IF_SP_WG1]);
		regfile.in_store_idx_push_trigger(in_dram_idx_push_trigger);
		regfile.out_store_idx(out_dram_idx);

		regfile.out_sd[0](regfile_sd[0]);
		regfile.out_sd[1](regfile_sd[1]);

		/* IExecute */
		iexecute.in_clk(in_clk);
		iexecute.in_pc(idecode_pc);
		iexecute.in_insn(idecode_insn);
		iexecute.in_wg(idecode_wg);
		iexecute.in_col_w(idecode_col_w);
		iexecute.in_subcol_w(idecode_subcol_w);
		for (l = 0; l < LANES; l++)
			iexecute.in_operand[2][l](regfile_data_r[2][l]);
		iexecute.in_sd[0](regfile_sd[0]);
		iexecute.in_sd[1](regfile_sd[1]);
		iexecute.in_thread_active(regfile_thread_active);
		iexecute.in_xlat_phys(xlat_phys);
		iexecute.in_sp_xlat_phys(xlat_sp_phys);
		iexecute.out_req_w(iexecute_req_w);
		iexecute.out_w(iexecute_w);
		iexecute.out_dequeue_sb(iexecute_dequeue_sb);
		iexecute.out_dequeue_sb_cstack_write(iexecute_dequeue_sb_cstack_write);
		iexecute.out_wg_w(iexecute_wg_w);
		iexecute.out_col_mask_w(iexecute_col_mask_w);
		iexecute.out_ignore_mask_w(iexecute_ignore_mask_w);
		for (l = 0; l < LANES; l++)
			iexecute.out_data_w[l](iexecute_data_w[l]);
		iexecute.out_cstack_entry(iexecute_cstack_entry);

		iexecute.out_pc_do_w(iexecute_pc_do_w);
		iexecute.out_pc_w(iexecute_pc_w);
		iexecute.out_wg_state_next[0](iexecute_wg_state_next[0]);
		iexecute.out_wg_state_next[1](iexecute_wg_state_next[1]);
		iexecute.out_wg_exit_commit(iexecute_wg_exit_commit);

		iexecute.out_cstack_action(iexecute_cstack_action);
		iexecute.in_cstack_top(cstack_top);
		iexecute.in_cstack_sp(cstack_sp);
		iexecute.in_cstack_full(cstack_full);
		iexecute.in_cstack_ex_overflow(cstack_ex_overflow);
		iexecute.in_wg_width(in_wg_width);
		iexecute.out_desc_fifo[IF_DRAM](out_desc_fifo);
		iexecute.out_desc_fifo[IF_SP_WG0](iexecute_sp_desc_fifo_0);
		iexecute.out_desc_fifo[IF_SP_WG1](iexecute_sp_desc_fifo_1);
		iexecute.out_store_kick[IF_DRAM](out_dram_kick);
		iexecute.out_store_kick[IF_SP_WG0](iexecute_store_kick_0);
		iexecute.out_store_kick[IF_SP_WG1](iexecute_store_kick_1);

		iexecute.set_scoreboard(&scoreboard);

		ctrlstack.in_clk(in_clk);
		ctrlstack.in_rst(in_rst);
		ctrlstack.in_wg(idecode_wg);
		ctrlstack.in_action(iexecute_cstack_action);
		ctrlstack.in_entry(iexecute_cstack_entry);
		ctrlstack.out_full(cstack_full);
		ctrlstack.out_sp(cstack_sp);
		ctrlstack.out_top(cstack_top);
		ctrlstack.out_ex_overflow(cstack_ex_overflow);

		scoreboard.in_clk(in_clk);
		scoreboard.in_dequeue(iexecute_dequeue_sb);
		scoreboard.in_enqueue(idecode_enqueue);
		scoreboard.in_dequeue_cstack_write(iexecute_dequeue_sb_cstack_write);
		scoreboard.in_dequeue_cstack_wg(iexecute_wg_w);
		scoreboard.in_enqueue_cstack_write(idecode_enqueue_cstack_write);
		scoreboard.in_enqueue_cstack_wg(idecode_enqueue_cstack_wg);
		scoreboard.out_cpop_stall[0](scoreboard_cpop_stall[0]);
		scoreboard.out_cpop_stall[1](scoreboard_cpop_stall[1]);
		scoreboard.in_req_w(idecode_req_w);
		scoreboard.in_req_r(idecode_req_r_sb);
		scoreboard.in_ssp_match(idecode_ssp_match);
		scoreboard.in_req_sb_pop[0](idecode_req_sb_pop[0]);
		scoreboard.in_req_sb_pop[1](idecode_req_sb_pop[1]);
		scoreboard.in_req_sb_pop[2](idecode_req_sb_pop[2]);
		scoreboard.out_raw(scoreboard_raw);
		scoreboard.out_ex_overflow(scoreboard_ex_overflow);
		scoreboard.out_entries_pop[0](scoreboard_entries_pop[0]);
		scoreboard.out_entries_pop[1](scoreboard_entries_pop[1]);
		scoreboard.in_entries_disable(iexecute_pc_do_w);
		scoreboard.in_entries_disable_wg(iexecute_wg_w);

		xlat.in_clk(in_clk);
		xlat.in_rst(in_rst);
		xlat.in_idx(idecode_xlat_idx);
		xlat.out_phys(xlat_phys);
		xlat.in_w(in_xlat_w);
		xlat.in_idx_w(in_xlat_idx_w);
		xlat.in_phys_w(in_xlat_phys_w);

		xlat_sp.in_clk(in_clk);
		xlat_sp.in_rst(in_rst);
		xlat_sp.in_idx(idecode_sp_xlat_idx);
		xlat_sp.out_phys(xlat_sp_phys);
		xlat_sp.in_w(in_sp_xlat_w);
		xlat_sp.in_idx_w(in_sp_xlat_idx_w);
		xlat_sp.in_phys_w(in_sp_xlat_phys_w);

		sp_0.in_clk(in_clk_dram);
		sp_0.in_sched_opts(in_sched_opts);
		sp_0.in_ticket_pop(out_ticket_pop);
		sp_0.in_desc_fifo(iexecute_sp_desc_fifo_0);
		sp_0.in_trigger(iexecute_store_kick_0);
		sp_0.out_wg_done(sp_wg_done_0);
		sp_0.out_rf_enable(sp_rf_enable[IF_SP_WG0]);
		sp_0.out_rf_write(sp_rf_write[IF_SP_WG0]);
		sp_0.out_rf_reg(sp_rf_reg[IF_SP_WG0]);
		sp_0.out_rf_mask(sp_rf_mask[IF_SP_WG0]);
		sp_0.in_rf_mask(regfile_store_mask[IF_SP_WG0]);
		sp_0.in_dram_enable(in_dram_enable);
		sp_0.in_dram_dst(in_dram_dst);
		sp_0.in_dram_write(in_dram_write);
		sp_0.in_dram_addr(in_dram_sp_addr);
		sp_0.in_dram_mask(in_dram_mask);

		for (i = 0; i < MC_BUS_WIDTH/4; i++) {
			sp_0.out_data[i](out_dram_data[IF_SP_WG0][i]);
			sp_0.in_dram_data[i](in_dram_data[i]);
		}

		for (i = 0; i < SP_BUS_WIDTH; i++) {
			sp_0.out_rf_idx[i](sp_rf_idx[IF_SP_WG0][i]);
			sp_0.in_rf_data[i](regfile_store_data[IF_SP_WG0][i]);
		}
#if SP_BUS_WIDTH != (MC_BUS_WIDTH/4)
		for (i = 0; i < SP_BUS_WIDTH - (BUS_WIDTH/4); i++)
			sp_0.out_data[(BUS_WIDTH/4)+i](sp_out_data[IF_SP_WG0][i]);
#endif

		sp_0.elaborate();

		sp_1.in_clk(in_clk_dram);
		sp_1.in_sched_opts(in_sched_opts);
		sp_1.in_ticket_pop(out_ticket_pop);
		sp_1.in_desc_fifo(iexecute_sp_desc_fifo_1);
		sp_1.in_trigger(iexecute_store_kick_1);
		sp_1.out_wg_done(sp_wg_done_1);
		sp_1.out_rf_enable(sp_rf_enable[IF_SP_WG1]);
		sp_1.out_rf_write(sp_rf_write[IF_SP_WG1]);
		sp_1.out_rf_reg(sp_rf_reg[IF_SP_WG1]);
		sp_1.out_rf_mask(sp_rf_mask[IF_SP_WG1]);
		sp_1.in_rf_mask(regfile_store_mask[IF_SP_WG1]);
		sp_1.in_dram_enable(in_dram_enable);
		sp_1.in_dram_dst(in_dram_dst);
		sp_1.in_dram_write(in_dram_write);
		sp_1.in_dram_addr(in_dram_sp_addr);
		sp_1.in_dram_mask(in_dram_mask);

		for (i = 0; i < MC_BUS_WIDTH/4; i++) {
			sp_1.out_data[i](out_dram_data[IF_SP_WG1][i]);
			sp_1.in_dram_data[i](in_dram_data[i]);
		}

		for (i = 0; i < SP_BUS_WIDTH; i++) {
			sp_1.out_rf_idx[i](sp_rf_idx[IF_SP_WG1][i]);
			sp_1.in_rf_data[i](regfile_store_data[IF_SP_WG1][i]);
		}
#if SP_BUS_WIDTH != (MC_BUS_WIDTH/4)
		for (i = 0; i < SP_BUS_WIDTH - (BUS_WIDTH/4); i++)
			sp_1.out_data[(BUS_WIDTH/4)+i](sp_out_data[IF_SP_WG1][i]);
#endif

		sp_1.elaborate();

		elaborated = true;
	}

private:
	/** Wire up the generic parts of the IDecoder. */
	void
	elaborate_idecode(void)
	{
		idecode->in_clk(in_clk);
		idecode->in_insn(imem_op);
		idecode->in_pc(imem_pc);
		idecode->in_wg(ifetch_wg);
		idecode->in_wg_width(in_wg_width);
		idecode->in_last_warp[0](simdcluster_last_warp[0]);
		idecode->in_last_warp[1](simdcluster_last_warp[1]);
		idecode->in_thread_active(regfile_thread_active);
		idecode->in_wg_finished(regfile_wg_finished);
		idecode->out_pc(idecode_pc);
		idecode->out_insn(idecode_insn);
		idecode->out_req(idecode_req_r);
		idecode->out_req_sb(idecode_req_r_sb);
		idecode->out_ssp_match(idecode_ssp_match);
		idecode->out_enqueue_sb(idecode_enqueue);
		idecode->out_enqueue_sb_cstack_write(idecode_enqueue_cstack_write);
		idecode->out_enqueue_sb_cstack_wg(idecode_enqueue_cstack_wg);
		idecode->in_sb_cpop_stall[0](scoreboard_cpop_stall[0]);
		idecode->in_sb_cpop_stall[1](scoreboard_cpop_stall[1]);
		idecode->out_req_w_sb(idecode_req_w);
		idecode->in_entries_pop[0](scoreboard_entries_pop[0]);
		idecode->in_entries_pop[1](scoreboard_entries_pop[1]);
		idecode->out_wg(idecode_wg);
		idecode->out_col_w(idecode_col_w);
		idecode->out_subcol_w(idecode_subcol_w);
		idecode->out_stall_f(idecode_stall_f);
		idecode->in_raw(scoreboard_raw);
		idecode->in_pipe_flush(iexecute_pc_do_w);
		idecode->out_xlat_idx(idecode_xlat_idx);
		idecode->out_sp_xlat_idx(idecode_sp_xlat_idx);
		idecode->in_req_conflicts(regfile_req_conflicts);

		/* Set a default number of IExecute pipeline stages. */
		iexecute_pipeline_stages(3);
	}

	/** Construct and elaborate a 1-stage IDecode component */
	void
	elaborate_idecode_1s(void)
	{
		unsigned int l;
		idecode = new IDecode_1S<PC_WIDTH,THREADS,LANES,RCPUS,XLAT_ENTRIES>("idecode");

		/* IDecode */
		elaborate_idecode();

		for (l = 0; l < LANES; l++) {
			iexecute.in_operand[0][l](regfile_data_r[0][l]);
			iexecute.in_operand[1][l](regfile_data_r[1][l]);
		}
	}

	/** Construct and elaborate a 3-stage IDecode component */
	void
	elaborate_idecode_3s(void)
	{
		unsigned int l;

		IDecode_3S<PC_WIDTH,THREADS,LANES,RCPUS,XLAT_ENTRIES> *idecode_3s =
				new IDecode_3S<PC_WIDTH,THREADS,LANES,RCPUS,XLAT_ENTRIES>("idecode");

		idecode = idecode_3s;

		/* IDecode */
		elaborate_idecode();

		idecode_3s->out_req_sb_pop[0](idecode_req_sb_pop[0]);
		idecode_3s->out_req_sb_pop[1](idecode_req_sb_pop[1]);
		idecode_3s->out_req_sb_pop[2](idecode_req_sb_pop[2]);

		for (l = 0; l < LANES; l++) {
			idecode_3s->in_operand[0][l](regfile_data_r[0][l]);
			idecode_3s->in_operand[1][l](regfile_data_r[1][l]);
			idecode_3s->out_operand[0][l](idecode_data_r[0][l]);
			idecode_3s->out_operand[1][l](idecode_data_r[1][l]);
			iexecute.in_operand[0][l](idecode_data_r[0][l]);
			iexecute.in_operand[1][l](idecode_data_r[1][l]);
		}
	}

	/** Update the performance counters.
	 *
	 * Called on every cycle. */
	void
	update_pcounters(void)
	{
		ifetch_wg_select wgs;

		for (unsigned int wg = 0; wg < 2; wg++) {
			switch (wg_state[wg]) {
			case WG_STATE_NONE:
				break;
			case WG_STATE_BLOCKED_DRAM:
			case WG_STATE_BLOCKED_DRAM_POSTEXIT:
				if (in_dram_dst.read().type != TARGET_NONE &&
				    in_dram_dst.read().wg == wg) {
					dram_active++;
				}
				break;
			case WG_STATE_BLOCKED_SP:
				sp_active[wg]++;
				break;
			case WG_STATE_RUN:
				wgs = ifetch.select_wg();
				if (wgs == wg) {
					compute_active++;
				}
				break;
			default:
				break;
			}
		}
	}

	/** Return the state of a work-group.
	 *
	 * @param wg Workgroup to print state to
	 * @return String showing a textual representation of the state of the
	 * workgroup provided.
	 */
	const string
	dbg_print_state(sc_uint<1> wg)
	{
		ifetch_wg_select wgs;

		switch (wg_state[wg]) {
		case WG_STATE_NONE:
			return "   idle";
			break;
		case WG_STATE_BLOCKED_DRAM:
			if (in_dram_dst.read().type != TARGET_NONE &&
			    in_dram_dst.read().wg == wg &&
			    !in_dram_ref.read()) {
				return "   DRAM";
			} else {
				return "blocked";
			}
			break;
		case WG_STATE_BLOCKED_DRAM_POSTEXIT:
			if (in_dram_dst.read().type != TARGET_NONE &&
			    in_dram_dst.read().wg == wg &&
			    !in_dram_ref.read()) {
				return "   DRAM+EXIT";
			} else {
				return "blocked";
			}
			break;
		case WG_STATE_BLOCKED_SP:
			if (sp_0.out_rf_reg.read().type != REGISTER_NONE &&
			    sp_0.out_rf_reg.read().wg == wg) {
				return "     SP";
			} else if (sp_1.out_rf_reg.read().type != REGISTER_NONE &&
				   sp_1.out_rf_reg.read().wg == wg) {
				return "     SP";
			} else {
				return "blocked";
			}
			break;
		case WG_STATE_RUN:
			wgs = ifetch.select_wg();
			if (wgs == wg) {
				return "    run";
			} else {
				return "  ready";
			}
			break;
		default:
			return "  error";
			break;
		}
	}

	/** Return the state of a work-group.
	 *
	 * @param wg Workgroup to print state to
	 * @return String showing a textual representation of the state of the
	 * workgroup provided.
	 */
	const string
	dbg_print_state_code(sc_uint<1> wg)
	{
		ifetch_wg_select wgs;

		switch (wg_state[wg]) {
		case WG_STATE_NONE:
			return "0";
			break;
		case WG_STATE_BLOCKED_DRAM:
		case WG_STATE_BLOCKED_DRAM_POSTEXIT:
			if (in_dram_dst.read().type != TARGET_NONE &&
			    in_dram_dst.read().wg == wg &&
			    !in_dram_ref.read()) {
				return "2";
			} else {
				return "4";
			}
			break;
		case WG_STATE_BLOCKED_SP:
			if (sp_0.out_rf_reg.read().type != REGISTER_NONE &&
			    sp_0.out_rf_reg.read().wg == wg) {
				return "3";
			} else if (sp_1.out_rf_reg.read().type != REGISTER_NONE &&
				   sp_1.out_rf_reg.read().wg == wg) {
				return "3";
			} else {
				return "4";
			}
			break;
		case WG_STATE_RUN:
			wgs = ifetch.select_wg();
			if (wgs == wg) {
				return "1";
			} else {
				return "0";
			}
			break;
		default:
			return "0";
			break;
		}
	}

	/** Retreive and print the status for the current workgroups. */
	void
	stats(void)
	{
		uint64 time;
		string wg0_status;
		string wg1_status;

		if (!debug_output[DEBUG_COMPUTE_WG_STATUS])
			return;

		wg0_status = dbg_print_state(0);
		wg1_status = dbg_print_state(1);

		time = sc_time_stamp().value() / 1000;
		cout << time << " ns Cluster X: [0] " <<
				wg0_status << " [1] " << wg1_status <<
				endl;
	}

	/** Retreive and print the status for the current workgroups. */
	void
	stats_code(void)
	{
		uint64 time;
		string wg0_status;
		string wg1_status;

		if (!debug_output[DEBUG_COMPUTE_WG_STATUS_CODE])
			return;

		wg0_status = dbg_print_state_code(0);
		wg1_status = dbg_print_state_code(1);

		time = sc_time_stamp().value() / 1000;
		cout << time << ",0," << wg0_status << endl;
		cout << time << ",1," << wg1_status << endl;
		cout << time << ",2,0" << endl << endl;
	}

	/** Perform a hardware reset */
	void
	do_reset(void)
	{
		wg_state[0] = WG_STATE_NONE;
		wg_state[1] = WG_STATE_NONE;

		wg_accept_next[0] = true;
		wg_accept_next[1] = true;

		ticket_pop = 0;

		simdcluster_rst.write(false);
	}

	/** Try and obtain a new workgroup from the WorkScheduler.
	 * @param slot Workgroup slot to fill. */
	void
	wg_try_obtain(unsigned int slot)
	{
		workgroup<THREADS,LANES> wg;
		sc_bv<WSS_SENTINEL> sched_opts;

		if (!in_wg.num_available()) {
			simdcluster_rst.write(false);
			return;
		}

		wg = in_wg.read();
		sched_opts = in_sched_opts.read();
		simdcluster_wg_off[slot][0].write(wg.off_x);
		simdcluster_wg_off[slot][1].write(wg.off_y);
		simdcluster_last_warp[slot].write(wg.last_warp);
		wg_state[slot] = WG_STATE_RUN;

		simdcluster_rst_wg.write(slot);
		simdcluster_rst.write(true);

		/* Under "Pairwise WG" scheduling, don't accept another WG until
		 * the other one hits an exit. */
		if (sched_opts[WSS_PAIRWISE_WG])
			wg_accept_next[slot] = false;

		if (debug_output[DEBUG_COMPUTE_WG_DIST])
			cout << sc_time_stamp() << " SimdCluster: [" << slot <<
				"] " << wg << endl;
	}

	/** Check DRAM status and unblock work-groups accordingly. */
	void
	wg_try_unblock(void)
	{
		RequestTarget dst_done;
		sc_uint<1> wg_done;

		if (in_dram_done_dst.num_available()) {
			dst_done = in_dram_done_dst.read();
			assert(wg_state[dst_done.wg] == WG_STATE_BLOCKED_DRAM ||
				wg_state[dst_done.wg] == WG_STATE_BLOCKED_DRAM_POSTEXIT);

			if (wg_state[dst_done.wg] == WG_STATE_BLOCKED_DRAM)
				wg_state[dst_done.wg] = WG_STATE_RUN;
			else
				wg_state[dst_done.wg] = WG_STATE_NONE;

			ticket_pop++;
		}

		if (sp_wg_done_0.num_available()) {
			wg_done = sp_wg_done_0.read();
			assert(wg_state[wg_done] == WG_STATE_BLOCKED_SP);

			wg_state[wg_done] = WG_STATE_RUN;

			ticket_pop++;
		}

		if (sp_wg_done_1.num_available()) {
			wg_done = sp_wg_done_1.read();
			assert(wg_state[wg_done] == WG_STATE_BLOCKED_SP);

			wg_state[wg_done] = WG_STATE_RUN;

			ticket_pop++;
		}

		out_ticket_pop.write(ticket_pop);
	}

	/** Update "blocked" status of WG if changed.
	 * @param wg Workgroup to update blocked status for.
	 */
	void
	wg_update_block(sc_uint<1> wg)
	{
		workgroup_state wg_state_next;

		wg_state_next = iexecute_wg_state_next[wg].read();

		switch (wg_state_next) {
			case WG_STATE_BLOCKED_DRAM_POSTEXIT:
				wg_accept_next[1 - wg.to_uint()] = true;
				/* fall-through */
			case WG_STATE_BLOCKED_DRAM:
			case WG_STATE_BLOCKED_SP:
				wg_state[wg] = wg_state_next;
				break;
			default:
				break;
		}

	}

	/** Update the workgroup status based in input signals for this cycle.
	 * @param mask_exit Mask indicating which WG had its exit bit set in
	 * 		    the previous cycle. */
	void
	wg_update_status(sc_bv<2> mask_exit)
	{
		sc_bv<2> mask;

		wg_update_block(0);
		wg_update_block(1);

		mask = regfile_wg_finished.read() & mask_exit;

		if (mask[0]) {
			wg_state[0] = WG_STATE_NONE;
			wg_accept_next[1] = true;
		}

		if (mask[1]) {
			wg_state[1] = WG_STATE_NONE;
			wg_accept_next[0] = true;
		}
	}

	/** Main thread */
	void
	thread_lt(void)
	{
		sc_bv<2> mask_exit = 0;

		assert(elaborated);

		sc_bv<32> pop = 0;
		pop.b_not();

		/** For IDecode_1S hard-wire these values to 1. */
		if (idec_impl == IDECODE_1S) {
			idecode_req_sb_pop[0].write(pop);
			idecode_req_sb_pop[1].write(pop);
			idecode_req_sb_pop[2].write(pop);
		}

		while (true) {
			wait();

			if (in_rst.read()) {
				do_reset();
				continue;
			}

			/** @todo Wait for wg_finished to be generated?
			 * Timing faff, default behaviour is the worst of
			 * both worlds. */
			wait(SC_ZERO_TIME);
			wg_try_unblock();
			wg_update_status(mask_exit);

			/* Only one per cycle */
			if (wg_state[0] == WG_STATE_NONE &&
					wg_accept_next[0])
				wg_try_obtain(0);
			else if (wg_state[1] == WG_STATE_NONE &&
					wg_accept_next[1])
				wg_try_obtain(1);
			else
				simdcluster_rst.write(false);

			simdcluster_wg_state[0].write(wg_state[0]);
			simdcluster_wg_state[1].write(wg_state[1]);

			if (in_end_prg.read() && wg_state[0] == WG_STATE_NONE &&
					wg_state[1] == WG_STATE_NONE)
				out_exec_fini.write(true);
			else
				out_exec_fini.write(false);

			wait(SC_ZERO_TIME);
			mask_exit = iexecute_wg_exit_commit.read();
						/* Save for next cycle */
			update_pcounters();
			stats();
			stats_code();
		}
	}

};

}

#endif /* COMPUTE_CONTROL_SIMDCLUSTER_H */
