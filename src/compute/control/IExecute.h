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

#ifndef COMPUTE_CONTROL_IEXECUTE_H
#define COMPUTE_CONTROL_IEXECUTE_H

#include <systemc>

#include "model/Register.h"
#include "model/stride_descriptor.h"
#include "compute/model/work.h"
#include "compute/model/ctrlstack_entry.h"
#include "compute/model/compute_stats.h"
#include "compute/control/Scoreboard.h"
#include "isa/model/Instruction.h"
#include "util/constmath.h"
#include "util/debug_output.h"
#include "util/Ringbuffer.h"

using namespace std;
using namespace sc_dt;
using namespace sc_core;
using namespace compute_model;
using namespace simd_model;
using namespace isa_model;
using namespace simd_util;

namespace compute_control {

typedef enum {
	PRINT_NONE,
	PRINT_SGPR,
	PRINT_VGPR,
	PRINT_PR,
	PRINT_CMASK,
	PRINT_TRACE,
} enum_print;

/** Pipeline structure for IExecute output signals.
 * @param PC_WIDTH Number of bits reserved for the PC.
 * @param THREADS Number of threads in a work-group.
 * @param LANES Number of FPUs in a SimdCluster.
 * @param RCPUS Number of ReCiProcal units in a SimdCluster.
 */
template <unsigned int PC_WIDTH, unsigned int THREADS,
		unsigned int LANES, unsigned int RCPUS>
class IExecute_pipe
{
public:
	/** True iff PC needs to be written. */
	bool pc_do_w;
	/** PC to write */
	sc_uint<PC_WIDTH> pc_w;

	/** True iff output register must be written to. */
	bool out_w;
	/** Output register to write */
	Register<THREADS/LANES> req_w;
	/** Active sub-warp for this write-operation. */
	sc_uint<const_log2(LANES/RCPUS)> subcol_w;
	/** Workgroup for this register. Seems duplicate, but used for...*/
	sc_uint<1> wg_w;
	/** Data to write */
	sc_uint<32> data_w[LANES];
	/** Column to write results to. */
	sc_uint<const_log2(THREADS/LANES)> col_mask_w;

	/** True iff a scoreboard entry must be taken from the queue. */
	bool dequeue_sb;

	/** True iff a cstack write entry must be consumed from the scoreboard. */
	bool dequeue_sb_cstack_entry;

	/** True iff the natural write mask should be ignore (e.g. when popping
	 * off the control stack). */
	bool ignore_mask_w;

	/** Instruct the control stack to pop or push. */
	ctrlstack_action cstack_action;
	/** Entry to write to the control stack in. */
	ctrlstack_entry<THREADS,PC_WIDTH> cstack_entry;

	/** Interface to send store through. IF_SENTINEL if no request should
	 * be performed. */
	req_if_t store_target;
	/** Stride descriptor of request. */
	stride_descriptor desc_fifo;

	/** Per-workgroup blocking reason (if any). */
	workgroup_state wg_state_next[2];

	/** Workgroup that commits an exit. */
	sc_bv<2> wg_exit_commit;

	/** Instruction for this pipeline entry. */
	Instruction op;

	/** Debug: type of print that must occur upon committing this pipeline
	 * stage. */
	enum_print print;

	/** Default empty constructor. */
	IExecute_pipe()
	: pc_do_w(false), pc_w(0),
	  out_w(false), req_w(Register<THREADS/LANES>()), wg_w(0),
	  col_mask_w(0), dequeue_sb(false), dequeue_sb_cstack_entry(false),
	  ignore_mask_w(false), cstack_action(CTRLSTACK_IDLE),
	  store_target(IF_SENTINEL), print(PRINT_NONE) {}

	/** Constructor.
	 * @param wg Active work-group slot for this pipeline stage.*/
	IExecute_pipe(sc_uint<1> wg)
	: pc_do_w(false), pc_w(0),
	  out_w(false), req_w(Register<THREADS/LANES>(wg)), wg_w(wg),
	  col_mask_w(0), dequeue_sb(false), dequeue_sb_cstack_entry(false),
	  ignore_mask_w(false), cstack_action(CTRLSTACK_IDLE),
	  store_target(IF_SENTINEL), print(PRINT_NONE)
	{
		wg_state_next[0] = WG_STATE_NONE;
		wg_state_next[1] = WG_STATE_NONE;
	}

	/** Invalidate this pipeline stage. */
	void
	invalidate(void)
	{
		pc_do_w = false;
		out_w = false;
		store_target = IF_SENTINEL;
		print = PRINT_NONE;
		wg_state_next[0] = WG_STATE_NONE;
		wg_state_next[1] = WG_STATE_NONE;
		cstack_action = CTRLSTACK_IDLE;
		op.kill();
	}
};

/** Instruction execute pipeline stage(s).
 * For now this execute phase is just a single pipeline stage. This is
 * unreasonable for many floating point operations, considering these consist of
 * preshift, wide arith, postshift.
 * @todo Be more reasonable about # pipeline stages.
 * @param PC_WIDTH Width of the program counter in bits.
 * @param THREADS Number of threads in a warp.
 * @param LANES Number of parallel lanes in one SIMD cluster.
 * @param CSTACK_ENTRIES Max. number of entries in the control stack.
 */
template <unsigned int PC_WIDTH, unsigned int THREADS = 1024,
		unsigned int LANES = 128, unsigned int RCPUS = 32,
		unsigned int CSTACK_ENTRIES = 16>
class IExecute : public sc_core::sc_module
{
private:
	/** Shadow cstack entry register used to store partial masks in
	 * anticipation of a CPUSH commit. */
	ctrlstack_entry<THREADS,PC_WIDTH> cstack_entry;

	/** Pipeline stages */
	Ringbuffer<IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> > pipe;

	/** A side-buffer in case the entry needs to be withheld from the
	 * pipeline.
	 * Used for SIDIV/SIMOD, which occupies the divider for 8 cycles.
	 * Keeping it on the side for a number of cycles will allow to drain
	 * the rest of the pipeline without flushing the div result too early. */
	IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> pipe_sidebuf;

	/** Counter that indicates when the sidebuf can enter the pipeline */
	int pipe_sidebuf_hold_counter;

	/** Pointer to the scoreboard. Used to validate CPOPs in a debugging
	 * build. */
	Scoreboard<THREADS,LANES> *sb;

	/** Performance counter. Committed vector sub-instructions. */
	unsigned long commit_vec[CAT_SENTINEL];

	/** Performance counter. Committed scalar instructions. */
	unsigned long commit_sc[CAT_SENTINEL];

	/** Performance counter. Number of NOPs and pipeline bubbles. */
	unsigned long commit_nop;

	/** Ticket counter for stride descriptors.
	 *
	 * XXX: When exploring multiple SimdCluster set-ups, a counter-based
	 * mechanism might no longer be feasible in real HW. We'd have to move
	 * the counter to a shared location like the WorkScheduler. */
	sc_uint<4> ticket_push;

public:
	/** Compute clock. */
	sc_in<bool> in_clk{"in_clk"};

	/** PC accompanying the instruction. */
	sc_in<sc_uint<PC_WIDTH> > in_pc{"in_pc"};

	/** Instruction fetched by IFetch. */
	sc_in<Instruction> in_insn{"in_insn"};

	/** Work-group associated with this instruction. */
	sc_in<sc_uint<1> > in_wg{"in_wg"};

	/** Identifier of currently active warp - important for write-back. */
	sc_in<sc_uint<const_log2(THREADS/LANES)> > in_col_w{"in_col_w"};

	/** Subcol for RCP unit instructions. */
	sc_in<sc_uint<const_log2(LANES/RCPUS)> > in_subcol_w{"in_subcol_w"};

	/** Inputs for instruction. For scalar->scalar, values in lane 0. For
	 * vector + scalar -> vector/pred, scalar replicated in all lanes.*/
	sc_in<sc_uint<32> > in_operand[3][LANES];

	/** Stride descriptor special register values. */
	sc_in<stride_descriptor> in_sd[2];

	/** At least one thread is active according to the active thread
	 * mask. */
	sc_in<sc_bv<2> > in_thread_active{"in_thread_active"};

	/** Physical address associated with incoming ld/st instruction */
	sc_in<Buffer> in_xlat_phys{"in_xlat_phys"};

	/** Physical address associated with incoming ld/st instruction */
	sc_in<Buffer> in_sp_xlat_phys{"in_sp_xlat_phys"};

	/***************** Signals for PC write-back ***********************/

	/** PC to write back in case of a branch operation */
	sc_inout<sc_uint<PC_WIDTH> > out_pc_w{"out_pc_w"};

	/** True iff a branch operation leads to writing a different PC */
	sc_inout<bool> out_pc_do_w{"out_pc_do_w"};

	/***************** Signals for register write-back *****************/

	/** Write request */
	sc_inout<Register<THREADS/LANES> > out_req_w{"out_req_w"};

	/** Workgroup associated with this request. */
	sc_inout<sc_uint<1> > out_wg_w{"out_wg_w"};

	/** Output of instruction. For scalar, result in lane 0. */
	sc_inout<sc_uint<32> > out_data_w[LANES];

	/** Results must be written back? */
	sc_inout<bool> out_w{"out_w"};

	/** Scoreboard entry must be removed? */
	sc_inout<bool> out_dequeue_sb{"out_dequeue_sb"};

	/** Scoreboard cstack entry must be removed? */
	sc_inout<bool> out_dequeue_sb_cstack_write
			{"out_dequeue_sb_cstack_write"};

	/** Results must really really be written back? Used for CPOP. */
	sc_inout<bool> out_ignore_mask_w{"out_ignore_mask_w"};

	/** Column to write results to. */
	sc_fifo_out<sc_uint<const_log2(THREADS/LANES)> >
				out_col_mask_w{"out_col_mask_w"};

	/***************** IO signals for control stack ********************/

	/** Action to perform on stack */
	sc_inout<ctrlstack_action> out_cstack_action{"out_cstack_action"};

	/** Entry to push to the cstack on a CPUSH operation */
	sc_inout<ctrlstack_entry<THREADS,PC_WIDTH> >
				out_cstack_entry{"out_cstack_entry"};

	/** Top of stack */
	sc_in<ctrlstack_entry<THREADS,PC_WIDTH> >
				in_cstack_top{"in_cstack_top"};

	/** Stack pointer */
	sc_in<sc_uint<const_log2(CSTACK_ENTRIES) + 1> >
				in_cstack_sp{"in_cstack_sp"};

	/** Is the cstack full? */
	sc_in<bool> in_cstack_full{"in_cstack_full"};

	/** Are we doing something illegal while the CSTACK is full/empty? */
	sc_in<bool> in_cstack_ex_overflow{"in_cstack_ex_overflow"};

	/*********************** Work parameters **************************/
	/** Workgroup offsets (X, Y) */
	sc_in<sc_uint<32> > in_wg_off[2][2];

	/** Work dimensions (X, Y) */
	sc_in<sc_uint<32> > in_dim[2];

	/** Dimension of the workgroups */
	sc_in<workgroup_width> in_wg_width{"in_wg_width"};

	/************************** DRAM Request **************************/
	/** DRAM request */
	sc_fifo_out<stride_descriptor> out_desc_fifo[IF_SENTINEL];

	/** Kick off request */
	sc_fifo_out<bool> out_store_kick[IF_SENTINEL];

	/** Per-workgroup blocking reason, if any. */
	sc_inout<workgroup_state> out_wg_state_next[2];

	/** Per_WG exit commit signal.
	 *
	 * Notify SimdCluster such that it can potentially update workgroup
	 * status. If done too early (e.g. as soon as the exit mask is all-0,
	 * rather than at the end of exit), the pipeline will contain some
	 * exit sub-instructions that will interfere with the cmask reset of
	 * the next workgroup.
	 */
	sc_inout<sc_bv<2> > out_wg_exit_commit{"out_wg_exit_commit"};

	/** Construct thread. */
	SC_CTOR(IExecute) : pipe(3), pipe_sidebuf_hold_counter(0), sb(nullptr),
			commit_nop(0ul), ticket_push(0)
	{
		unsigned int i;

		reset_cstack_entry();

		for (i = 0; i < CAT_SENTINEL; i++) {
			commit_vec[i] = 0ul;
			commit_sc[i] = 0ul;
		}

		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();
	}

	/** Set the number of pipeline stages for IExecute
	 * @param stages Number of desired stages. Minimum of 3. */
	void
	set_pipeline_stages(unsigned int stages)
	{
		/** Judging by NVIDIAs US patent 7,117,238, it's
		 * possible to do a fully pipelined RCP/RSQRT in 3 cycles
		 * provided 4 multipliers. For fair comparison, assume pipeline
		 * is at least 3 deep. */
		if (stages < 3)
			throw new invalid_argument("Number of pipeline stages "
					"must be greater than or equal to 3.");

		pipe.resize(stages);
	}

	/** Set a reference to the scoreboard object, for debugging purposes.
	 * @param s Pointer to the active scoreboard component. */
	void
	set_scoreboard(Scoreboard<THREADS,LANES> *s)
	{
		sb = s;
	}

	/** Debug: obtain run statistics and store them in s
	 * @param s Reference to compute_stats object that will hold new run
	 * 	    statistics. */
	void
	get_stats(compute_stats &s)
	{
		unsigned int i;

		s.commit_nop = commit_nop;
		for (i = 0; i < CAT_SENTINEL; i++) {
			s.commit_vec[i] = commit_vec[i];
			s.commit_sc[i] = commit_sc[i];
		}
	}

private:
	/** Reset the masked CSTACK register used for CPUSH. */
	void
	reset_cstack_entry(void)
	{
		cstack_entry.pred_mask = 0;
		cstack_entry.mask_type = VSP_CTRL_RUN;
		cstack_entry.pc = 0;
	}

	/** Execute MAD.
	 * @param mod Instruction modifier.
	 * @param ps Pipeline stage. */
	void
	do_VMAD(ISASubOpFPUMod mod, IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		bfloat m1, m2, a, res;

		for (unsigned int i = 0; i < LANES; i++) {
			m1.b = in_operand[0][i].read();
			m2.b = in_operand[1][i].read();
			a.b = in_operand[2][i].read();

			if (mod == FPU_NEG)
				m2.f = -m2.f;

			res.f = m1.f*m2.f+a.f;

			ps.data_w[i] = res.b;
		}
	}

	/** Execute ADD.
	 * @param mod Instruction modifier.
	 * @param ps Pipeline stage. */
	void
	do_VADD(ISASubOpFPUMod mod, IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		bfloat a, b, res;

		for (unsigned int i = 0; i < LANES; i++) {
			a.b = in_operand[0][i].read();
			b.b = in_operand[1][i].read();

			if (mod == FPU_NEG)
				b.f = -b.f;

			res.f = a.f+b.f;

			ps.data_w[i] = res.b;
		}
	}

	/** Execute MUL.
	 * @param mod Instruction modifier.
	 * @param ps Pipeline stage. */
	void
	do_VMUL(ISASubOpFPUMod mod, IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		bfloat m1, m2, res;

		for (unsigned int i = 0; i < LANES; i++) {
			m1.b = in_operand[0][i].read();
			m2.b = in_operand[1][i].read();
			if (mod == FPU_NEG)
				m2.f = -m2.f;

			res.f = m1.f*m2.f;

			ps.data_w[i] = res.b;
		}
	}

	/** Execute MIN.
	 * @param ps Pipeline stage. */
	void
	do_VMIN(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		bfloat m1, m2, res;

		for (unsigned int i = 0; i < LANES; i++) {
			m1.b = in_operand[0][i].read();
			m2.b = in_operand[1][i].read();

			res.f = min(m1.f,m2.f);

			ps.data_w[i] = res.b;
		}
	}

	/** Execute MAX.
	 * @param ps Pipeline stage. */
	void
	do_VMAX(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		bfloat m1, m2, res;

		for (unsigned int i = 0; i < LANES; i++) {
			m1.b = in_operand[0][i].read();
			m2.b = in_operand[1][i].read();

			res.f = max(m1.f,m2.f);

			ps.data_w[i] = res.b;
		}
	}

	/** Execute ABS.
	 * @param ps Pipeline stage. */
	void
	do_VABS(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		for (unsigned int i = 0; i < LANES; i++) {
			ps.data_w[i] = in_operand[0][i].read() & (~0x80000000u);
		}
	}

	/** Untyped MOV.
	 * @param ps Pipeline stage. */
	void
	do_MOV(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		for (unsigned int l = 0; l < LANES; l++)
			ps.data_w[l] = in_operand[0][l].read();
	}

	/** Typed MOV.
	 * @param subop Sub-operation indicating a int->float or float->int
	 * conversion.
	 * @param ps Pipeline stage. */
	void
	do_CVT(ISASubOpCVT subop, IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		bfloat intm;

		if (subop == CVT_I2F) {
			for (unsigned int l = 0; l < LANES; l++) {
				intm.f = in_operand[0][l].read();
				ps.data_w[l] = intm.b;
			}
		} else {
			for (unsigned int l = 0; l < LANES; l++) {
				intm.b = in_operand[0][l].read();
				ps.data_w[l] = intm.f;
			}
		}
	}

	/** Typed MOV.
	 * @param subop Sub-operation indicating a int->float or float->int
	 * conversion.
	 * @param ps Pipeline stage. */
	void
	do_SCVT(ISASubOpCVT subop, IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		bfloat intm;

		if (subop == CVT_I2F) {
			intm.f = in_operand[0][0].read();
			ps.data_w[0] = intm.b;
		} else {
			intm.b = in_operand[0][0].read();
			ps.data_w[0] = intm.f;
		}
	}

	/**
	 * Perform a buffer query
	 * @param subop Queried property.
	 * @param ps Pipeline stage "register" to store result in.
	 */
	void
	do_BUFQUERY(ISASubOpBUFQUERY subop,
			IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		Buffer b;

		b = in_xlat_phys.read();
		if (!b.valid) {
			cout << "Error: querying unmapped buffer" << endl;
			exit(-1);
		}

		switch (subop) {
		case BUFQUERY_DIM_X:
			ps.data_w[0] = b.dims[0];
			break;
		case BUFQUERY_DIM_Y:
			ps.data_w[0] = b.dims[1];
			break;
		default:
			assert(false);
			break;
		}
	}

	/**
	 * Execute TEST operation.
	 * Result lives in the output operand, no point in creating extra
	 * channels. If this were to live in a separate functional unit to
	 * parallelise with MAD, we can reconsider.
	 * @param test Subop for this test operation.
	 * @param ps Pipeline stage "register" to store result in.
	 */
	void
	do_TEST(ISASubOpTEST test, IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		bfloat in;

		for (unsigned int i = 0; i < LANES; i++) {
			in.b = in_operand[0][i].read();

			switch (test) {
			case TEST_EZ:
				ps.data_w[i] = (in.f == 0.f || in.f == -0.f);
				break;
			case TEST_NZ:
				ps.data_w[i] = (in.f != 0.f && in.f != -0.f);
				break;
			case TEST_L:
				ps.data_w[i] = (in.f < 0.f);
				break;
			case TEST_LE:
				ps.data_w[i] = (in.f <= 0.f || in.f == -0.f);
				break;
			case TEST_G:
				ps.data_w[i] = (in.f > 0.f);
				break;
			case TEST_GE:
				ps.data_w[i] = (in.f >= 0.f || in.f == -0.f);
				break;
			default:
				break;
			}
		}
	}

	/**
	 * Execute ITEST operation.
	 * Result lives in the output operand, no point in creating extra
	 * channels. If this were to live in a separate functional unit to
	 * parallelise with MAD, we can reconsider.
	 * @param test Subop for this test operation.
	 * @param ps Pipeline stage. */
	void
	do_ITEST(ISASubOpTEST test, IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int in;

		for (unsigned int i = 0; i < LANES; i++) {
			in = in_operand[0][i].read();

			switch (test) {
			case TEST_EZ:
				ps.data_w[i] = (in == 0);
				break;
			case TEST_NZ:
				ps.data_w[i] = (in != 0);
				break;
			case TEST_L:
				ps.data_w[i] = (in < 0);
				break;
			case TEST_LE:
				ps.data_w[i] = (in <= 0);
				break;
			case TEST_G:
				ps.data_w[i] = (in > 0);
				break;
			case TEST_GE:
				ps.data_w[i] = (in >= 0);
				break;
			default:
				break;
			}
		}
	}

	/**
	 * Execute TEST operation.
	 * Result lives in the output operand, no point in creating extra
	 * channels. If this were to live in a separate functional unit to
	 * parallelise with MAD, we can reconsider.
	 * @param subop (Boolean) operation to perform on the predicate regs.
	 * @param ps Pipeline stage. */
	void
	do_PBOOL(ISASubOpPBOOL subop,
			IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		unsigned int b1, b2;

		for (unsigned int i = 0; i < LANES; i++) {
			b1 = in_operand[0][i].read() & 0x1;
			b2 = in_operand[1][i].read() & 0x1;

			switch(subop) {
			case PBOOL_AND:
				ps.data_w[i] = b1 & b2;
				break;
			case PBOOL_OR:
				ps.data_w[i] = b1 | b2;
				break;
			case PBOOL_NAND:
				ps.data_w[i] = !(b1 & b2);
				break;
			case PBOOL_NOR:
				ps.data_w[i] = !(b1 | b2);
				break;
			default:
				break;
			}
		}
	}

	/** Scalar Jump with IMM parameter.
	 * @param ps Pipeline stage. */
	void
	do_J(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		ps.pc_w = in_operand[0][0].read();
		ps.pc_do_w = true;
	}

	/** Scalar Jump with IMM parameter.
	 * @param test Subop specifying which test to perform.
	 * @param ps Pipeline stage. */
	void
	do_SICJ(ISASubOpTEST test,
			IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int in;
		bool res = false;

		in = in_operand[1][0].read();

		switch (test) {
		case TEST_EZ:
			res = (in == 0);
			break;
		case TEST_NZ:
			res = (in != 0);
			break;
		case TEST_L:
			res = (in < 0);
			break;
		case TEST_LE:
			res = (in <= 0);
			break;
		case TEST_G:
			res = (in > 0);
			break;
		case TEST_GE:
			res = (in >= 0);
			break;
		default:
			break;
		}

		if (res) {
			ps.pc_w = in_operand[0][0].read();
			ps.pc_do_w = true;
		}
	}

	/** Pop an entry off the control stack.
	 *
	 * CPOP is a vector instruction for the sake of re-using existing
	 * infrastructure for input (generally a predicate register) and output.
	 * The cstack is designed to always have the top of the stack on its
	 * output bits. CPOP will then read these values and stores them to
	 * the correct SP_CTRL bitmask (as indicated in the stack entry's type)
	 * LANES entries at a time. The commit bit indicates that the last set
	 * of bits has been read, typically when col_w == last_warp. The top
	 * entry is only effectively popped off the cstack when this bit is set.
	 *
	 * @param commit True iff the pop must be commited to the cstack.
	 * @param ps Pipeline stage. */
	void
	do_CPOP(bool commit, IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		unsigned int l;
		unsigned int col = in_col_w.read();
		unsigned int offset = col * LANES;

		ctrlstack_entry<THREADS,PC_WIDTH> cstack_entry;

		cstack_entry = in_cstack_top.read();

		for (l = 0; l < LANES; l++, offset++)
			ps.data_w[l] = cstack_entry.pred_mask[offset] ? 1 : 0;

		ps.req_w = Register<THREADS/LANES>(in_wg.read(), REGISTER_VSP,
				cstack_entry.mask_type, col);
		ps.ignore_mask_w = true;
		ps.out_w = true;

		if (commit) {
			ps.cstack_action = CTRLSTACK_POP;
			ps.pc_w = cstack_entry.pc;
			ps.pc_do_w = true;
		}
	}

	/** Push to control stack.
	 *
	 * CPUSH (like CPOP) is a vector instruction for the sake of re-using
	 * existing infrastructure for input (generally a predicate register)
	 * and output. This means that the values to push will come in LANES
	 * bits at a time. The commit bit indicates that the last set of bits is
	 * presented, typically when col_w == last_warp. So results are only
	 * pushed upon commit.
	 * @param subop Type of control stack entry to push.
	 * @param pc PC for pushed stack entry.
	 * @param commit True iff the entry is complete and must be pushed.
	 * @param ps Pipeline stage. */
	void
	do_CPUSH(ISASubOpCPUSH subop, sc_uint<PC_WIDTH> pc, bool commit,
			IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		unsigned int l;
		unsigned int col = in_col_w.read();
		unsigned int offset = col * LANES;

		for (l = 0; l < LANES; l++, offset++)
			cstack_entry.pred_mask[offset] =
					(in_operand[1][l].read() ? 1 : 0);

		cstack_entry.pc = pc;

		switch (subop) {
		case CPUSH_IF:
			cstack_entry.mask_type = VSP_CTRL_RUN;
			break;
		case CPUSH_BRK:
			cstack_entry.mask_type = VSP_CTRL_BREAK;
			break;
		case CPUSH_RET:
			cstack_entry.mask_type = VSP_CTRL_RET;
			break;
		default:
			assert(false);
			break;
		}

		if (commit) {
			ps.cstack_action = CTRLSTACK_PUSH;
			ps.cstack_entry = cstack_entry;
		}
	}

	/** (Conditional) mask (continue, break, exit). Inverts the result.
	 * @param src_idx Index of source register containing mask.
	 * @param ps Pipeline stage. */
	void
	do_CMASK(unsigned int src_idx,
			IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		unsigned int l;

		/* Invert predicate register to output and write to ctrl reg */
		for (l = 0; l < LANES; l++)
			ps.data_w[l] = in_operand[src_idx][l].read() ? 0 : 1;
	}

	/** (Conditional) mask for calls.
	 * @param src_idx Index of source register containing mask.
	 * @param ps Pipeline stage. */
	void
	do_CALL_MASK(unsigned int src_idx,
			IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		unsigned int l;

		/* Predicate register to output and write to ctrl reg */
		for (l = 0; l < LANES; l++)
			ps.data_w[l] = in_operand[src_idx][l].read() ? 1 : 0;
	}

	/** Execute IADD.
	 * @param ps Pipeline stage. */
	void
	do_IADD(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a, b;

		for (unsigned int i = 0; i < LANES; i++) {
			a = in_operand[0][i].read();
			b = in_operand[1][i].read();

			ps.data_w[i] = a + b;
		}
	}

	/** Execute ISUB.
	 * @param ps Pipeline stage. */
	void
	do_ISUB(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a, b;

		for (unsigned int i = 0; i < LANES; i++) {
			a = in_operand[0][i].read();
			b = in_operand[1][i].read();

			ps.data_w[i] = a - b;
		}
	}

	/** Execute IMUL.
	 * @param ps Pipeline stage. */
	void
	do_IMUL(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a, b;

		for (unsigned int i = 0; i < LANES; i++) {
			a = in_operand[0][i].read();
			b = in_operand[1][i].read();

			ps.data_w[i] = a * b;
		}
	}

	/** Execute IMAD.
	 * @param ps Pipeline stage. */
	void
	do_IMAD(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a, b, c;

		for (unsigned int i = 0; i < LANES; i++) {
			a = in_operand[0][i].read();
			b = in_operand[1][i].read();
			c = in_operand[2][i].read();

			ps.data_w[i] = a * b + c;
		}
	}

	/** Execute IMIN.
	 * @param ps Pipeline stage. */
	void
	do_IMIN(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int m1, m2, res;

		for (unsigned int i = 0; i < LANES; i++) {
			m1 = in_operand[0][i].read();
			m2 = in_operand[1][i].read();

			res = min(m1,m2);

			ps.data_w[i] = res;
		}
	}

	/** Execute IMAX.
	 * @param ps Pipeline stage. */
	void
	do_IMAX(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int m1, m2, res;

		for (unsigned int i = 0; i < LANES; i++) {
			m1 = in_operand[0][i].read();
			m2 = in_operand[1][i].read();

			res = max(m1,m2);

			ps.data_w[i] = res;
		}
	}


	/** Execute SSHL.
	 * @param ps Pipeline stage. */
	void
	do_SHL(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a, b;

		// b is a scalar
		b = in_operand[1][0].read();

		for (unsigned int i = 0; i < LANES; i++) {
			a = in_operand[0][i].read();

			ps.data_w[i] = a << b;
		}
	}

	/** Execute SSHR.
	 * @param ps Pipeline stage. */
	void
	do_SHR(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a, b;

		// b is a scalar
		b = in_operand[1][0].read();

		for (unsigned int i = 0; i < LANES; i++) {
			a = in_operand[0][i].read();

			ps.data_w[i] = a >> b;
		}
	}

	/** Execute AND.
	 * @param ps Pipeline stage. */
	void
	do_AND(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a, b;

		for (unsigned int i = 0; i < LANES; i++) {
			a = in_operand[0][i].read();
			b = in_operand[1][i].read();

			ps.data_w[i] = a & b;
		}
	}

	/** Execute OR.
	 * @param ps Pipeline stage. */
	void
	do_OR(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a, b;

		for (unsigned int i = 0; i < LANES; i++) {
			a = in_operand[0][i].read();
			b = in_operand[1][i].read();

			ps.data_w[i] = a | b;
		}
	}

	/** Execute XOR.
	 * @param ps Pipeline stage. */
	void
	do_XOR(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a, b;

		for (unsigned int i = 0; i < LANES; i++) {
			a = in_operand[0][i].read();
			b = in_operand[1][i].read();

			ps.data_w[i] = a ^ b;
		}
	}

	/** Execute NOT.
	 * @param ps Pipeline stage. */
	void
	do_NOT(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a;

		for (unsigned int i = 0; i < LANES; i++) {
			a = in_operand[0][i].read();

			ps.data_w[i] = ~a;
		}
	}

	/** Execute SMOV.
	 * @param ps Pipeline stage. */
	void
	do_SMOV(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		ps.data_w[0] = in_operand[0][0].read();
	}

	/** Execute SIADD.
	 * @param ps Pipeline stage. */
	void
	do_SIADD(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a, b;

		a = in_operand[0][0].read();
		b = in_operand[1][0].read();

		ps.data_w[0] = a + b;
	}

	/** Execute SISUB.
	 * @param ps Pipeline stage. */
	void
	do_SISUB(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a, b;

		a = in_operand[0][0].read();
		b = in_operand[1][0].read();

		ps.data_w[0] = a - b;
	}

	/** Execute SIMUL.
	 * @param ps Pipeline stage. */
	void
	do_SIMUL(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a, b;

		a = in_operand[0][0].read();
		b = in_operand[1][0].read();

		ps.data_w[0] = a * b;
	}

	/** Execute SIMAD.
	 * @param ps Pipeline stage. */
	void
	do_SIMAD(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int m1, m2, a3;

		m1 = in_operand[0][0].read();
		m2 = in_operand[1][0].read();
		a3 = in_operand[2][0].read();

		ps.data_w[0] = m1 * m2 + a3;
	}

	/** Execute SIMIN.
	 * @param ps Pipeline stage. */
	void
	do_SIMIN(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int m1, m2, res;

		m1 = in_operand[0][0].read();
		m2 = in_operand[1][0].read();

		res = min(m1,m2);

		ps.data_w[0] = res;
	}

	/** Execute SIMAX.
	 * @param ps Pipeline stage. */
	void
	do_SIMAX(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int m1, m2, res;

		m1 = in_operand[0][0].read();
		m2 = in_operand[1][0].read();

		res = max(m1,m2);

		ps.data_w[0] = res;
	}

	/** Execute SINEG.
	 * @param ps Pipeline stage. */
	void
	do_SINEG(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int m1;

		m1 = in_operand[0][0].read();

		ps.data_w[0] = -m1;
	}

	/** Execute SIBFIND.
	 * @param ps Pipeline stage. */
	void
	do_SIBFIND(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		unsigned int a;

		a = in_operand[0][0].read();

		if (a & 0x80000000)
			a = ~a;

		a = (a << 1) | 1;

		ps.data_w[0] = 30 - __builtin_clz(a);
	}

	/** Execute SSHL.
	 * @param ps Pipeline stage. */
	void
	do_SSHL(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a, b;

		a = in_operand[0][0].read();
		b = in_operand[1][0].read();

		ps.data_w[0] = a << b;
	}

	/** Execute SSHR.
	 * @param ps Pipeline stage. */
	void
	do_SSHR(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a, b;

		a = in_operand[0][0].read();
		b = in_operand[1][0].read();

		ps.data_w[0] = a >> b;
	}

	/** Perform scalar integer division.
	 * @param ps Pipeline stage. */
	void
	do_SIDIV(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a, b;

		a = in_operand[0][0].read();
		b = in_operand[1][0].read();

		ps.data_w[0] = a / b;

		pipe_sidebuf_hold_counter = max((int)(8 - pipe.getEntries()), 0);
	}

	/** Perform scalar integer modulo.
	 * @param ps Pipeline stage. */
	void
	do_SIMOD(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a, b;

		a = in_operand[0][0].read();
		b = in_operand[1][0].read();

		ps.data_w[0] = a % b; /**< @todo % or mod? */

		pipe_sidebuf_hold_counter = max((int)(8 - pipe.getEntries()), 0);
	}

	/** Perform scalar boolean AND.
	 * @param ps Pipeline stage. */
	void
	do_SAND(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a, b;

		a = in_operand[0][0].read();
		b = in_operand[1][0].read();

		ps.data_w[0] = a & b;
	}

	/** Perform scalar boolean OR.
	 * @param ps Pipeline stage. */
	void
	do_SOR(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a, b;

		a = in_operand[0][0].read();
		b = in_operand[1][0].read();

		ps.data_w[0] = a | b;
	}

	/** Perform scalar boolean NOT.
	 * @param ps Pipeline stage. */
	void
	do_SNOT(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		int a;

		a = in_operand[0][0].read();

		ps.data_w[0] = ~a;
	}

	/** Execute RCP.
	 * @param ps Pipeline stage. */
	void
	do_RCP(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		/* We cheat a little here for the sake of simulation speed.
		 * Rather than doing RCPUS subcolumns in every cycle, we perform
		 * the calculation of *all* lanes upon commit. This saves
		 * copying data around in the pipeline and allows the compiler
		 * to more aggressively optimise the inner loop using SIMD
		 * techniques.
		 */
		bfloat a, res;

		if (!ps.out_w)
			return;

		for (unsigned int i = 0; i < LANES; i++) {
			a.b = in_operand[0][i].read();

			res.f = 1.f/a.f;

			ps.data_w[i] = res.b;
		}
	}

	/** Execute RSQRT.
	 * @param ps Pipeline stage. */
	void
	do_RSQRT(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		/* And here too we simplify. */
		bfloat a, res;

		if (!ps.out_w)
			return;

		for (unsigned int i = 0; i < LANES; i++) {
			a.b = in_operand[0][i].read();

			res.f = 1.f/sqrt(a.f);

			ps.data_w[i] = res.b;
		}
	}

	/** Execute SIN.
	 * @param ps Pipeline stage. */
	void
	do_SIN(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		bfloat a, res;

		if (!ps.out_w)
			return;

		for (unsigned int i = 0; i < LANES; i++) {
			a.b = in_operand[0][i].read();

			res.f = sin(a.f);

			ps.data_w[i] = res.b;
		}
	}

	/** Execute COS.
	 * @param ps Pipeline stage. */
	void
	do_COS(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		/* And here too we simplify. */
		bfloat a, res;

		if (!ps.out_w)
			return;

		for (unsigned int i = 0; i < LANES; i++) {
			a.b = in_operand[0][i].read();

			res.f = cos(a.f);

			ps.data_w[i] = res.b;
		}
	}

	/** Perform a global load/store "linear", following dimensions of the
	 * workgroup.
	 * @param op Instruction containing this load/store.
	 * @param ps Pipeline stage. */
	void
	do_LDSTLIN(Instruction &op,
			IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		unsigned int wg;
		Register<THREADS/LANES> dst;
		sc_uint<32> wg_width;
		sc_int<32> offset_x;
		sc_int<32> offset_y;
		Buffer b;
		sc_uint<32> wl;

		ISASubOpLDSTLIN subop = op.getSubOp().ldstlin;

		wg = in_wg.read();
		wg_width = 32 << in_wg_width.read();
		dst = op.getDst().getRegister<THREADS/LANES>(wg, 0);
		b = in_xlat_phys.read();

		offset_x = (in_wg_off[wg][0].read() << 5) +
				((sc_int<32>)in_operand[1][0].read());
		offset_y = in_wg_off[wg][1].read() +
				((sc_int<32>)in_operand[2][0].read());

		stride_descriptor sd(dst);

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

		sd.write = (op.getOp() == OP_STGLIN);
		sd.period = b.get_dim_x();
		sd.period_count = min(sc_uint<32>(THREADS/wg_width),
				(sc_uint<32>) (in_dim[1].read() - in_wg_off[wg][1].read()));
		sd.words = min(sc_uint<32>(wl * wg_width),
				(sc_uint<32>) (b.get_dim_x() - (offset_x * wl)));
		sd.dst_period = 32 << in_wg_width.read();

		sd.dst_offset = 0;
		if (offset_y < 0) {
			sd.dst_off_y = -offset_y;
			sd.period_count += offset_y;
			offset_y = 0;
		}

		if (offset_x < 0) {
			sd.dst_off_x = -offset_x;
			sd.words += offset_x;
			offset_x = 0;
		}
		sd.addr = b.getAddress() +
			((offset_y * b.get_dim_x() + offset_x * wl) << 2);

		if (dst.type == REGISTER_VSP)
			sd.dst_offset = (offset_y * b.get_dim_x() + offset_x * wl);

		ldst_kick(op, IF_DRAM, sd, ps);
	}

	/** Perform a scratchpad load/store "linear", following dimensions of
	 * the workgroup.
	 * @param op Instruction containing this load/store operation.
	 * @param ps Pipeline stage. */
	void
	do_LDSTSPLIN(Instruction &op,
			IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		unsigned int wg;
		Register<THREADS/LANES> dst;
		sc_uint<32> wg_width;
		sc_int<32> offset_x;
		sc_int<32> offset_y;
		Buffer b;

		wg = in_wg.read();
		wg_width = 32 << in_wg_width.read();
		dst = op.getDst().getRegister<THREADS/LANES>(wg, 0);
		b = in_sp_xlat_phys.read();

		offset_x = (sc_int<32>)in_operand[1][0].read();
		offset_y = (sc_int<32>)in_operand[2][0].read();

		stride_descriptor sd(dst);

		sd.write = (op.getOp() == OP_STSPLIN);
		sd.period = b.get_dim_x();
		sd.period_count = min(sc_uint<32>(THREADS/wg_width),
				(sc_uint<32>) (b.get_dim_y() - offset_y));
		sd.words = min(sc_uint<32>(wg_width),
				(sc_uint<32>) (b.get_dim_x() - offset_x));
		sd.dst_period = 32 << in_wg_width.read();

		sd.dst_offset = 0;
		if (offset_y < 0) {
			sd.dst_off_y = -offset_y;
			sd.period_count += offset_y;
			offset_y = 0;
		}

		if (offset_x < 0) {
			sd.dst_off_x = -offset_x;
			sd.words += offset_x;
			offset_x = 0;
		}
		sd.addr = b.getAddress() +
			((offset_y * b.get_dim_x() + offset_x) << 2);

		if (dst.type == REGISTER_VSP)
			sd.dst_offset = (offset_y * b.get_dim_x() + offset_x);

		ldst_kick(op, req_if_t(wg), sd, ps);
	}

	/** Perform a load/store, streaming an entire buffer past the CAMs'
	 * shared data bus.
	 * @param op Instruction containing this snoopy iterative load/store.
	 * @param ps Pipeline stage. */
	void
	do_LDSTBIDX(Instruction &op,
			IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		unsigned int wg;
		Register<THREADS/LANES> dst;
		Buffer b;

		wg = in_wg.read();

		dst = op.getDst().getRegister<THREADS/LANES>(wg, 0);
		b = in_xlat_phys.read();

		stride_descriptor sd(dst);

		sd.write = (op.getOp() == OP_STGBIDX);
		sd.period = b.dims[0];
		sd.period_count = b.dims[1];
		sd.words = b.dims[0];
		sd.dst_period = 32 << in_wg_width.read();

		sd.dst_offset = 0;
		sd.addr = b.getAddress();

		ldst_kick(op, IF_DRAM, sd, ps);
	}

	/** Perform a load/store using a custom stride, data plucked from the
	 * bus using CAMs.
	 * @param op Instruction containing this snoopy indexed transfer.
	 * @param ps Pipeline stage. */
	void
	do_LDSTCIDX(Instruction &op,
			IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		unsigned int wg;
		Register<THREADS/LANES> dst;
		sc_int<32> offset_x;
		sc_int<32> offset_y;
		Buffer b;
		stride_descriptor params;

		wg = in_wg.read();
		params = in_sd[wg].read();

		dst = op.getDst().getRegister<THREADS/LANES>(wg, 0);
		b = in_xlat_phys.read();

		offset_x = (sc_int<32>)in_operand[1][0].read();
		offset_y = (sc_int<32>)in_operand[2][0].read();

		stride_descriptor sd(dst);

		sd.write = (op.getOp() == OP_STGCIDX);
		sd.period = params.period;
		sd.period_count = params.period_count;
		sd.words = params.words;
		sd.dst_period = 0;

		if (offset_y < 0) {
			sd.period_count += offset_y;
			offset_y = 0;
		}

		if (offset_x < 0) {
			sd.words += offset_x;
			offset_x = 0;
		}
		sd.addr = b.getAddress() +
			((offset_y * b.get_dim_x() + offset_x) << 2);

		sd.dst_offset = (offset_y * b.get_dim_x() + offset_x);

		ldst_kick(op, IF_DRAM, sd, ps);
	}

	/** Perform a DRAM load/store, iterating over the indexes one by one.
	 * @param op Instruction containing this indexed iterative operation.
	 * @param ps Pipeline stage. */
	void
	do_LDSTGIDXIT(Instruction &op,
			IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		unsigned int wg;
		Register<THREADS/LANES> dst;
		Buffer b;

		wg = in_wg.read();

		dst = op.getDst().getRegister<THREADS/LANES>(wg, 0);
		b = in_xlat_phys.read();

		stride_descriptor sd(dst);

		sd.type = stride_descriptor::IDXIT;
		sd.write = (op.getOp() == OP_STGIDXIT);

		sd.dst_offset = 0;
		sd.addr = b.getAddress();

		ldst_kick(op, IF_DRAM, sd, ps);
	}

	/** Transfer a tile of data to the scratchpad.
	 * @param op Instruction containing this tiled DRAM->SP data transfer.
	 * @param ps Pipeline stage. */
	void
	do_LDSPG2SPTILE(Instruction &op,
			IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		unsigned int wg;
		sc_int<32> offset_x;
		sc_int<32> offset_y;
		Buffer b, spb;

		wg = in_wg.read();
		b = in_xlat_phys.read();
		spb = in_sp_xlat_phys.read();

		offset_x = (sc_int<32>)in_operand[1][0].read();
		offset_y = (sc_int<32>)in_operand[2][0].read();

		stride_descriptor sd;
		sd.dst = RequestTarget(wg,TARGET_SP);

		sd.write = (op.getOp() == OP_STG2SPTILE);
		sd.period = b.get_dim_x();
		sd.period_count = min(sc_uint<32>(spb.get_dim_y()),
				(sc_uint<32>) (b.get_dim_y() - (offset_y)));
		sd.words = min(sc_uint<32>(spb.get_dim_x()),
				(sc_uint<32>) (b.get_dim_x() - (offset_x)));
		sd.dst_period = spb.get_dim_x();

		sd.dst_offset = spb.addr;

		/** Negative edge cases. */
		if (offset_y < 0) {
			sd.dst_offset += (sd.dst_period * (-offset_y) * 4);
			sd.period_count += offset_y;
			offset_y = 0;
		}
		if (offset_x < 0) {
			sd.dst_offset -= offset_x * 4;
			sd.words += offset_x;
			offset_x = 0;
		}
		sd.addr = b.getAddress() +
			((offset_y * b.get_dim_x() + offset_x) << 2);

		ldst_kick(op, IF_DRAM, sd, ps);
	}

	/** Perform a scratchpad load/store, streaming an entire buffer past the
	 * CAMs' shared data bus.
	 * @param op Instruction containing this load/store
	 * @param ps Pipeline stage. */
	void
	do_LDSTSPBIDX(Instruction &op,
			IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		unsigned int wg;
		Register<THREADS/LANES> dst;
		Buffer b;

		wg = in_wg.read();

		dst = op.getDst().getRegister<THREADS/LANES>(wg, 0);
		b = in_sp_xlat_phys.read();

		stride_descriptor sd(dst);

		sd.write = (op.getOp() == OP_STSPBIDX);
		sd.period = b.dims[0];
		sd.period_count = b.dims[1];
		sd.words = b.dims[0];
		sd.dst_period = b.dims[0]; // XXX?

		sd.dst_offset = 0;
		sd.addr = b.getAddress();

		ldst_kick(op, req_if_t(wg), sd, ps);
	}

	/** Perform a small DRAM load to the scalar register file.
	 * @param op Instruction containing this scalar DRAM load
	 * @param ps Pipeline stage.*/
	void
	do_SLD(Instruction &op, IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		unsigned int wg;
		Register<THREADS/LANES> dst;
		sc_uint<32> wg_width;
		sc_uint<32> offset_x;
		sc_uint<32> offset_y;
		Buffer b;

		wg = in_wg.read();
		wg_width = 32 << in_wg_width.read();
		dst = op.getDst().getRegister<THREADS/LANES>(wg, 0);
		b = in_xlat_phys.read();

		offset_x = 0;/*in_operand[1][0] << 2;*/

		stride_descriptor sd(dst);

		sd.write = false;
		sd.addr = b.getAddress() + offset_x;
		sd.period = in_operand[1][0].read();
		sd.period_count = 1;
		sd.words = in_operand[1][0].read();

		ldst_kick(op, IF_DRAM, sd, ps);
	}

	/** Perform a small scratchpad load to the scalar register file.
	 * @param op Instruction containing this scalar scratchpad load.
	 * @param ps Pipeline stage.*/
	void
	do_SLDSP(Instruction &op,
			IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		unsigned int wg;
		Register<THREADS/LANES> dst;
		stride_descriptor params;
		sc_uint<32> offset_x;
		sc_uint<32> offset_y;
		Buffer b;

		wg = in_wg.read();
		params = in_sd[wg].read();
		b = in_sp_xlat_phys.read();

		offset_x = in_operand[1][0].read() << 2;
		offset_y = (in_operand[2][0].read() * b.dims[0]) << 2;

		dst = op.getDst().getRegister<THREADS/LANES>(wg, 0);
		stride_descriptor sd(dst);

		sd.write = false;
		sd.addr = b.getAddress() + offset_x + offset_y;
		sd.period = params.words;
		sd.period_count = 1;
		sd.words = params.words;

		ldst_kick(op, req_if_t(wg), sd, ps);
	}

	/** Shared load-store kick-off method.
	 * @param op Instruction for this load/store operation
	 * @param target Target interface (DRAM, scratchpad).
	 * @param sd Stride descriptor to issue.
	 * @param ps Pipeline stage. */
	void
	ldst_kick(Instruction &op, req_if_t target, stride_descriptor &sd,
			IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		sc_uint<1> wg;

		wg = in_wg.read();

		sd.ticket = ticket_push;

		ps.desc_fifo = sd;
		ps.store_target = target;

		switch (target) {
		case IF_DRAM:
			if (op.postExit())
				ps.wg_state_next[wg] = WG_STATE_BLOCKED_DRAM_POSTEXIT;
			else
				ps.wg_state_next[wg] = WG_STATE_BLOCKED_DRAM;
			break;
		default:
			ps.wg_state_next[wg] = WG_STATE_BLOCKED_SP;
			break;
		}

		/* No jump is taken, but writing back a PC will solve a lot of
		 * pipelining problems */
		ps.pc_w = in_pc.read() + 1;
		ps.pc_do_w = true;
	}

	/** Enable/disable trace.
	 *
	 * We're being a bit imprecise with committing this.
	 * @param ps Pipeline stage. */
	void
	do_PRINTTRACE(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		ps.data_w[0] = !!in_operand[0][0].read();
		ps.print = PRINT_TRACE;
	}

	/** Perform the operation specified in op.
	 * @param op Operation to perform.
	 * @param ps Pipeline stage. */
	void
	doExecute(Instruction op, IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		unsigned int i;

		if (op.isDead())
			return;

		switch (op.getOp()) {
		case OP_TEST:
			do_TEST(op.getSubOp().test, ps);
			break;
		case OP_ITEST:
			do_ITEST(op.getSubOp().test, ps);
			break;
		case OP_PBOOL:
			do_PBOOL(op.getSubOp().pbool, ps);
			break;
		case OP_J:
			do_J(ps);
			break;
		case OP_SICJ:
			do_SICJ(op.getSubOp().test, ps);
			break;
		case OP_BRA:
			do_CPUSH(CPUSH_IF, in_operand[0][0].read(),
					op.getCommit(), ps);
			do_CMASK(1, ps);
			break;
		case OP_CALL:
			do_CPUSH(CPUSH_RET, in_pc.read() + 1, op.getCommit(), ps);
			do_CALL_MASK(1, ps);
			if (op.getCommit())
				do_J(ps);
			break;
		case OP_CPOP:
			do_CPOP(op.getCommit(), ps);
			break;
		case OP_CPUSH:
			do_CPUSH(op.getSubOp().cpush,in_operand[0][0].read(),
					op.getCommit(), ps);
			break;
		case OP_EXIT:
			if (op.getCommit())
				ps.wg_exit_commit[in_wg.read()] = Log_1;
			/* fall-through. */
		case OP_BRK:
		case OP_CMASK:
			do_CMASK(0, ps);
			break;
		case OP_MAD:
			do_VMAD(op.getSubOp().fpumod,ps);
			break;
		case OP_ADD:
			do_VADD(op.getSubOp().fpumod,ps);
			break;
		case OP_MUL:
			do_VMUL(op.getSubOp().fpumod,ps);
			break;
		case OP_MIN:
			do_VMIN(ps);
			break;
		case OP_MAX:
			do_VMAX(ps);
			break;
		case OP_ABS:
			do_VABS(ps);
			break;
		case OP_MOV:
		case OP_MOVVSP:
			do_MOV(ps);
			break;
		case OP_CVT:
			do_CVT(op.getSubOp().cvt, ps);
			break;
		case OP_SCVT:
			do_SCVT(op.getSubOp().cvt, ps);
			break;
		case OP_BUFQUERY:
			do_BUFQUERY(op.getSubOp().bufquery, ps);
			break;
		case OP_IADD:
			do_IADD(ps);
			break;
		case OP_ISUB:
			do_ISUB(ps);
			break;
		case OP_IMUL:
			do_IMUL(ps);
			break;
		case OP_IMAD:
			do_IMAD(ps);
			break;
		case OP_IMIN:
			do_IMIN(ps);
			break;
		case OP_IMAX:
			do_IMAX(ps);
			break;
		case OP_SHL:
			do_SHL(ps);
			break;
		case OP_SHR:
			do_SHR(ps);
			break;
		case OP_AND:
			do_AND(ps);
			break;
		case OP_OR:
			do_OR(ps);
			break;
		case OP_XOR:
			do_XOR(ps);
			break;
		case OP_NOT:
			do_NOT(ps);
			break;
		case OP_SMOV:
		case OP_SMOVSSP:
			do_SMOV(ps);
			break;
		case OP_SIADD:
			do_SIADD(ps);
			break;
		case OP_SISUB:
			do_SISUB(ps);
			break;
		case OP_SIMUL:
			do_SIMUL(ps);
			break;
		case OP_SIMAD:
			do_SIMAD(ps);
			break;
		case OP_SIMIN:
			do_SIMIN(ps);
			break;
		case OP_SIMAX:
			do_SIMAX(ps);
			break;
		case OP_SINEG:
			do_SINEG(ps);
			break;
		case OP_SIBFIND:
			do_SIBFIND(ps);
			break;
		case OP_SSHL:
			do_SSHL(ps);
			break;
		case OP_SSHR:
			do_SSHR(ps);
			break;
		case OP_SIDIV:
			do_SIDIV(ps);
			break;
		case OP_SIMOD:
			do_SIMOD(ps);
			break;
		case OP_SAND:
			do_SAND(ps);
			break;
		case OP_SOR:
			do_SOR(ps);
			break;
		case OP_SNOT:
			do_SNOT(ps);
			break;
		case OP_RCP:
			do_RCP(ps);
			break;
		case OP_RSQRT:
			do_RSQRT(ps);
			break;
		case OP_SIN:
			do_SIN(ps);
			break;
		case OP_COS:
			do_COS(ps);
			break;
		case OP_LDGLIN:
		case OP_STGLIN:
			do_LDSTLIN(op, ps);
			break;
		case OP_LDSPLIN:
		case OP_STSPLIN:
			do_LDSTSPLIN(op, ps);
			break;
		case OP_SLDG:
			do_SLD(op, ps);
			break;
		case OP_SLDSP:
			do_SLDSP(op, ps);
			break;
		case OP_LDGBIDX:
		case OP_STGBIDX:
			do_LDSTBIDX(op, ps);
			break;
		case OP_LDGCIDX:
		case OP_STGCIDX:
			do_LDSTCIDX(op, ps);
			break;
		case OP_LDGIDXIT:
		case OP_STGIDXIT:
			do_LDSTGIDXIT(op, ps);
			break;
		case OP_LDG2SPTILE:
		case OP_STG2SPTILE:
			do_LDSPG2SPTILE(op, ps);
			break;
		case OP_LDSPBIDX:
		case OP_STSPBIDX:
			do_LDSTSPBIDX(op, ps);
			break;
		case OP_DBG_PRINTSGPR:
			ps.data_w[0] = in_operand[0][0].read();
			ps.print = PRINT_SGPR;
			break;
		case OP_DBG_PRINTVGPR:
			assert(in_operand[1][0].read() < THREADS);
			ps.data_w[0] = in_operand[0][in_operand[1][0].read() & (LANES - 1)].read();
			ps.print = PRINT_VGPR;
			break;
		case OP_DBG_PRINTPR:
			for (i = 0; i < LANES; i++)
				ps.data_w[i] = in_operand[0][i].read();
			ps.print = PRINT_PR;
			break;
		case OP_DBG_PRINTCMASK:
			for (i = 0; i < LANES; i++)
				ps.data_w[i] = in_operand[0][i].read();
			ps.print = PRINT_CMASK;
			break;
		case OP_DBG_PRINTTRACE:
			do_PRINTTRACE(ps);
			break;
		case NOP:
			break;
		default:
			cout << "IExecute ERROR: Unhandled op " <<
					op.opToString() << endl;
			break;
		}
	}

	/**
	 * Generate signals for register write-back.
	 *
	 * When an instruction is declared dead due to a pipeline flush or
	 * otherwise, or if the instruction has no destination register, this
	 * will disable write-back for the next cycle such that the operation
	 * doesn't take effect.
	 * @param op Operation triggering this write-back.
	 * @param ps Pipeline stage.
	  */
	void
	setWrite(Instruction &op,
			IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		Operand dst;
		sc_uint<1> wg;

		ps.dequeue_sb = op.getOnSb();
		ps.dequeue_sb_cstack_entry = op.getOnCStackSb();

		wg = in_wg.read();
		dst = op.getDst();
		ps.req_w = dst.getRegister<THREADS/LANES>(wg, in_col_w.read());

		if (op.isDead() || !op.hasDst() || op.ldst())
			return;

		assert(dst.getType() != OPERAND_IMM);

		if (dst.isVectorType())
			ps.col_mask_w = in_col_w.read();

		ps.subcol_w = in_subcol_w.read();

		if (opCategory(op.getOp()) == CAT_ARITH_RCPU)
			ps.out_w = op.getCommit();
		else
			ps.out_w = true;
	}

#ifndef NDEBUG
	/** If we're going to pop this entry off, better make sure it's on
	 * there. Helps catch bugs where we're popping sth we didn't push.
	 */
	void
	debug_sb_contains_reg(Register<THREADS/LANES> &reg, Instruction &op)
	{
		if (sb && !sb->debug_contains(reg)) {
			cerr << sc_time_stamp() << "Attempting to pop an invalid entry "
					<< reg << endl;
			cerr << "         " << op << endl;;
			assert(false);
		}
	}
#endif

	/** Update performance counters.
	 * @param ps Committed pipeline stage. */
	void
	commit_pcount(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		if (ps.op.isDead() || ps.op.getOp() == NOP)
			commit_nop++;
		else if (ps.op.isVectorInstruction())
			commit_vec[int(opCategory(ps.op.getOp()))]++;
		else
			commit_sc[int(opCategory(ps.op.getOp()))]++;
	}

	/** Commit a pipeline stage to the IExecute outputs.
	 * @param ps Pipeline stage.
	 */
	void
	commit(IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> &ps)
	{
		unsigned int l;
		bfloat item;

		out_pc_w.write(ps.pc_w);
		out_pc_do_w.write(ps.pc_do_w);

		out_req_w.write(ps.req_w);
		out_wg_w.write(ps.wg_w);
		if (ps.req_w.isVectorType())
			for (l = 0; l < LANES; l++)
				out_data_w[l].write(ps.data_w[l]);
		else
			out_data_w[0].write(ps.data_w[0]);

		out_w.write(ps.out_w);
		out_dequeue_sb.write(ps.dequeue_sb);
		out_dequeue_sb_cstack_write.write(ps.dequeue_sb_cstack_entry);

#ifndef NDEBUG
		if (ps.dequeue_sb)
			debug_sb_contains_reg(ps.req_w, ps.op);
#endif

		out_ignore_mask_w.write(ps.ignore_mask_w);
		if (ps.out_w && ps.req_w.isVectorType() && !ps.ignore_mask_w)
			out_col_mask_w.write(ps.col_mask_w);

		out_cstack_action.write(ps.cstack_action);
		out_cstack_entry.write(ps.cstack_entry);

		if (ps.store_target != IF_SENTINEL) {
			out_desc_fifo[ps.store_target].write(ps.desc_fifo);
			out_store_kick[ps.store_target].nb_write(true);
			ticket_push++;
		}
		out_wg_state_next[0].write(ps.wg_state_next[0]);
		out_wg_state_next[1].write(ps.wg_state_next[1]);
		out_wg_exit_commit.write(ps.wg_exit_commit);

		switch (ps.print)
		{
		case PRINT_SGPR:
			item.b = ps.data_w[0];
			cout << "@" << sc_time_stamp() << " Print SGPR(" << ps.wg_w << "): "
					<< item.b << "/" << item.f << endl;
			break;
		case PRINT_VGPR:
			item.b = ps.data_w[0];

			cout << "@" << sc_time_stamp() << " Print VGPR(" << ps.wg_w << "): "
					<< item.b << "/" << item.f << endl;
			break;
		case PRINT_PR:
			cout << "@" << sc_time_stamp() << " Print PR(" << ps.wg_w << "): ";
			for (l = 0; l < LANES; l++) {
				item.b = ps.data_w[l];
				cout << item.b;
			}

			cout << endl;

			break;
		case PRINT_CMASK:
			cout << "@" << sc_time_stamp() << " Print CMASK(" << ps.wg_w << "): ";
			for (l = 0; l < LANES; l++) {
				item.b = ps.data_w[l];
				cout << item.b;
			}

			cout << endl;

			break;
		case PRINT_TRACE:
			debug_output[DEBUG_COMPUTE_TRACE] = ps.data_w[0];
			break;
		default:
			break;
		}

		if (sc_is_running())
			commit_pcount(ps);
	}

	/** Invalidate current pipeline contents. */
	void
	pipe_invalidate(void)
	{
		int i;

		for (i = pipe.getEntries() - 1; i >= 0; i--) {
			auto &pipe_elem = pipe.getStage(i);

			/* Don't invalidate injected CPOPs */
			if (pipe_elem.op.isInjected())
				continue;

			pipe_elem.invalidate();
		}
	}

	/** Return true iff an instruction writes a CMASK.
	 * @param op Instruction to test
	 * @return True iff this instruction writes to a CMASK. */
	bool
	writesCMASK(Instruction &op)
	{
		/* pipe_commit_idx == pipe_write_idx, pipeline stage hasn't
		 * been written yet */
		if (pipe.getEntries() == 1)
			return op.writesCMASK();
		else
			return pipe.top().req_w.isCMASK();
	}

	/** Decrement the hold counter for the pipe_sidebuf. */
	inline void
	decrement_pipe_sidebuf_hold_counter(void)
	{
		pipe_sidebuf_hold_counter =
				max(pipe_sidebuf_hold_counter - 1, 0);
	}

	/** Main thread. */
	void
	thread_lt(void)
	{
		Instruction op;
		IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS> pipe_elem;

		while (true) {
			if (pipe_sidebuf_hold_counter == 0) {
				pipe_sidebuf =
					IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS>(in_wg.read());

				op = in_insn.read();
				pipe_elem = pipe.top();

				/* Insert a post-branch bubble. */
				if (out_pc_do_w.read() ||
				    !in_thread_active.read()[in_wg.read()]) {

					op.kill();
					pipe_invalidate();
				}

				pipe_sidebuf.op = op;

				setWrite(op, pipe_sidebuf);
				doExecute(op, pipe_sidebuf);
			}

			decrement_pipe_sidebuf_hold_counter();

			if (pipe_sidebuf_hold_counter == 0) {
				pipe_elem = pipe.swapHead(pipe_sidebuf);
			} else {
				/* NOP */
				pipe_elem = IExecute_pipe<PC_WIDTH,THREADS,LANES,RCPUS>(in_wg.read());
				pipe_elem = pipe.swapHead(pipe_elem);
			}

			commit(pipe_elem);

			if (debug_output[DEBUG_COMPUTE_TRACE]) {
				cout << sc_time_stamp() << " IExecute: " <<
					in_pc.read() << in_col_w.read() << " " << op <<
					" " << in_operand[0][0].read() << " " << in_operand[1][0].read() <<
					" " << in_operand[2][0].read() << endl;
				cout << sc_time_stamp() << " IExecute: COMMITTING WG" <<
					pipe_elem.wg_w << ": " << pipe_elem.op << endl;

			}
			wait();
		}
	}
};

}

#endif /* COMPUTE_CONTROL_IEXECUTE_H */
