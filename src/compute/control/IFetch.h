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

#ifndef COMPUTE_CONTROL_IFETCH_H
#define COMPUTE_CONTROL_IFETCH_H

#include <systemc>

#include "compute/model/work.h"
#include "compute/model/imem_request.h"
#include "util/constmath.h"
#include "util/debug_output.h"

using namespace sc_core;
using namespace sc_dt;
using namespace std;
using namespace compute_model;

namespace compute_control {

typedef enum {
	IFETCH_WG_NONE = -1,
	IFETCH_WG_0 = 0,
	IFETCH_WG_1 = 1,
} ifetch_wg_select;

/** Instruction fetch pipeline stage.
 * @todo Should IFetch be the ``workgroup master'', or is that a level up? */
template <unsigned int PC_WIDTH>
class IFetch : public sc_core::sc_module
{
private:
	/** Compute-wide PC, per-workgroup. */
	sc_uint<PC_WIDTH> pc[2];

	/** Active work-group slot. */
	unsigned int wg;

public:
	/** Compute clock. */
	sc_in<bool> in_clk{"in_clk"};

	/** Stall signal from decode.
	 *
	 * Triggered under two conditions:
	 * - Enumerating warps of a vector instruction. Possibly an injected
	 * CPOP.
	 * - RAW hazard.
	 *
	 * (Injected) CPOP instructions always end with a jump, hence off-by-one
	 * on PC can be tolerated. */
	sc_in<bool> in_stall_d{"in_stall"};

	/** State of the workgroups for this cluster */
	sc_in<workgroup_state> in_wg_state[2];

	/** Finished bit, comes slightly earlier than state. */
	sc_in<sc_bv<2> > in_wg_finished{"in_wg_finished"};

	/** Boolean, true iff PC should be overwritten (branch). */
	sc_in<bool> in_pc_write{"in_pc_write"};

	/** PC to overwrite current PC with.
	 *
	 * All instructions that cause a workgroup to block, such as DRAM
	 * instructions, are expected to write the PC of the first instruction
	 * post-blocking, to ensure a correct PC upon data return regardless of
	 * the pipeline depth. */
	sc_in<sc_uint<PC_WIDTH> > in_pc_w{"in_pc_w"};

	/** Workgroup for PC write */
	sc_in<sc_uint<1> > in_pc_wg_w{"in_pc_wg_w"};

	/** Out PC, connecting to IMem input. */
	sc_fifo_out<imem_request<PC_WIDTH> > out_insn_r{"out_insn_r"};

	/** Workgroup currently active. */
	sc_inout<sc_uint<1> > out_wg{"out_wg"};

	/** Workgroup to reset PC for. */
	sc_in<sc_uint<1> > in_pc_rst_wg{"pc_rst_wg"};

	/** Boolean: true iff PC for wg in in_pc_rst_wg must be reset. */
	sc_in<bool> in_pc_rst{"pc_rst"};

	/** When true, don't schedule compute in parallel with SP read/write. */
	sc_in<sc_bv<WSS_SENTINEL> > in_sched_opts{"in_sched_opts"};

	/** Construct thread. */
	SC_CTOR(IFetch) : wg(0)
	{
		pc[0] = 0;
		pc[1] = 0;

		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();
	}

	/** Select the workgroup to execute ths cycle */
	ifetch_wg_select
	select_wg(void)
	{
		sc_bv<WSS_SENTINEL> sched_opts;

		sched_opts = in_sched_opts.read();

		/** Don't issue instructions along with SP r/w. */
		if (sched_opts[WSS_NO_PARALLEL_COMPUTE_SP] &&
		      (in_wg_state[0] == WG_STATE_BLOCKED_SP ||
		       in_wg_state[1] == WG_STATE_BLOCKED_SP))
				return IFETCH_WG_NONE;

		if (in_wg_state[wg] == WG_STATE_RUN &&
				!in_wg_finished.read()[wg])
			return ifetch_wg_select(wg);
		else if (in_wg_state[1-wg] == WG_STATE_RUN &&
				!in_wg_finished.read()[1-wg])
			return ifetch_wg_select(1-wg);

		return IFETCH_WG_NONE;
	}

private:
	/** Main thread
	 * @todo This logic feels a tad ad-hoc. Perhaps rethink and simplify.
	 *
	 * We are managing two PC registers and an output. Ideally the output
	 * is just a selection of either PC, but auto-increment is tricky in
	 * the light of switching threads. Here's what we know:
	 * - Any command that causes blocking (e.g. DRAM) writes a PC in the
	 *   first cycle it's blocked. This means that upon resuming the thread
	 *   will trivially be restarted with the correct PC.
	 * - in_stall_d implies that the PC on the output should not be
	 *   incremented yet. Whether the register is incremented is up for
	 *   debate, but bear in mind that a thread switch could occur straight
	 *   after in_stall_d is deasserted if e.g. stalling on an EXIT op. */
	void
	thread_lt(void)
	{
		ifetch_wg_select active_wg;
		imem_request<PC_WIDTH> req;

		while (true) {
			wait();

			/* Writes happen first, unconditionally */
			if (in_pc_rst.read())
				pc[in_pc_rst_wg.read()] = 0;

			if (in_pc_write.read())
				pc[in_pc_wg_w.read()] = in_pc_w.read();

			/* Select the eligible WG */
			active_wg = select_wg();

			if (active_wg == IFETCH_WG_NONE) {
				req.valid = false;
				out_insn_r.write(req);

				if (debug_output[DEBUG_COMPUTE_TRACE])
					cout << sc_time_stamp() <<
						" IFetch: idle" << endl;
				continue;
			}

			/** Bias select_wg() towards current WG */
			wg = active_wg;

			if (debug_output[DEBUG_COMPUTE_TRACE])
				cout << sc_time_stamp() << " IFetch: wg(" <<
					wg << ") PC: " << pc[wg] << endl;

			if (!in_stall_d.read() || in_pc_write.read()) {
				req.pc = pc[wg]++;
				req.valid = true;
				out_insn_r.write(req);
				out_wg.write(wg);
			}
		}
	}
};

}

#endif /* COMPUTE_CONTROL_IFETCH_H */
