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

#ifndef COMPUTE_CONTROL_WORKSCHEDULER_H
#define COMPUTE_CONTROL_WORKSCHEDULER_H

#include "compute/model/work.h"
#include "compute/model/compute_stats.h"
#include "model/Buffer.h"

#include "util/ddr4_lid.h"
#include "util/sched_opts.h"

using namespace std;
using namespace sc_dt;
using namespace sc_core;
using namespace compute_model;
using namespace dram;

namespace compute_control {

/** State of the WorkScheduler */
typedef enum {
	WS_STATE_IDLE,
	WS_STATE_LOAD_KERNEL,
	WS_STATE_ENUM_WGS,
	WS_STATE_WAIT_FINI,
	WS_STATE_SENTINEL
} ws_state;

/** Enumerate work into workgroups. (For now) serves as a front-end.
 * @todo Kernel should obviously come from a DRAM buffer, but given we don't
 * have an opcode format this doesn't make sense to simulate right now. We
 * could add support for deriving a latency by directly querying Ramulator
 * (which we'd be linking anyway) or indirectly by querying MC for an estimate.
 */
template <unsigned int THREADS, unsigned int LANES, unsigned int PC_WIDTH,
	unsigned int XLAT_ENTRIES>
class WorkScheduler : public sc_core::sc_module
{
private:
	/** Current state of the workscheduler. */
	ws_state state;

	/** Stats for kernel execution */
	compute_stats stats;

	/** Cycle counter */
	unsigned long cycle;

	/** Cycle for kick-off of kernel */
	unsigned long start_cycle;

	/** Number of bytes per opcode. */
	unsigned int opcode_bytes;

public:
	/** Compute clock. */
	sc_in<bool> in_clk{"in_clk"};

	/** Work specification. */
	sc_in<work<XLAT_ENTRIES> > in_work{"in_work"};

	/** Kick off work. */
	sc_in<bool> in_kick{"in_kick"};

	/** Workgroup generated
	 * @todo FIFO? */
	sc_fifo_out<workgroup<THREADS,LANES> > out_wg{"out_wg"};

	/** Workgroup width. */
	sc_inout<workgroup_width> out_wg_width{"out_wg_width"};

	/** Scheduling options. */
	sc_inout<sc_bv<WSS_SENTINEL> > out_sched_opts{"out_sched_opts"};

	/** X,Y dimensions of work. */
	sc_inout<sc_uint<32> > out_dim[2];

	/** Instruction to upload to IMem, double data rate */
	sc_inout<Instruction> out_imem_op[4];

	/** PC for instruction */
	sc_inout<sc_uint<PC_WIDTH> > out_imem_pc{"out_imem_pc"};

	/** Write bit */
	sc_inout<bool> out_imem_w{"out_imem_w"};

	/** True iff all workgroups have been enumerated into the FIFO. */
	sc_inout<bool> out_end_prg{"out_end_prg"};

	/** True iff execution is finished. */
	sc_in<bool> in_exec_fini{"in_exec_fini"};

	/** Write a translation table entry */
	sc_inout<bool> out_xlat_w{"out_xlat_w"};

	/** Buffer index to write to. */
	sc_inout<sc_uint<const_log2(XLAT_ENTRIES)> >
					out_xlat_idx_w{"out_xlat_idx_w"};

	/** Physical address indexed by buffer index. */
	sc_inout<Buffer> out_xlat_phys_w{"out_xlat_phys_w"};

	/** Write a translation table entry */
	sc_inout<bool> out_sp_xlat_w{"out_sp_xlat_w"};

	/** Buffer index to write to. */
	sc_inout<sc_uint<const_log2(XLAT_ENTRIES)> >
					out_sp_xlat_idx_w{"out_sp_xlat_idx_w"};

	/** Physical address indexed by buffer index. */
	sc_inout<Buffer> out_sp_xlat_phys_w{"out_sp_xlat_phys_w"};

	/** Constructor */
	SC_CTOR(WorkScheduler) : state(WS_STATE_IDLE), cycle(0ull),
			start_cycle(0ull), opcode_bytes(8)
	{
		stats = {0,0,0,0};

		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();

		SC_THREAD(thread_cycle_counter);
		sensitive << in_clk.pos();
	}

	/** Copy the current set of stats to the provided compute stats object.
	 * @param s Reference to a compute_stats object to store performance
	 * counter data into.
	 */
	void
	get_stats(compute_stats &s)
	{
		s.exec_time = stats.exec_time;
		s.prg_load_time = stats.prg_load_time;
		s.threads = stats.threads;
		s.wgs = stats.wgs;
	}

	/** Compute the execution time in number of cycles. */
	void
	stats_set_cycle_time(void)
	{
		stats.exec_time = cycle - start_cycle;
		/** XXX: This stopping criterion is insufficient once you
		 * add support for multiple contexts/kernels in a single
		 * simulation run. */
		if (out_sched_opts.read()[WSS_STOP_SIM_FINI])
			sc_stop();
	}

private:
	/** Estimate the number of cycles for a DRAM transfer.
	 *
	 * We can use this to estimate the number of cycles we require for
	 * program loading. The exact method is described in the dissertation,
	 * chapter for stride transfer evaluation of Sim-D.
	 * @param bytes Number of bytes we estimate the program to be.
	 * @todo Query the DRAM timings from ramulator.
	 * @todo Do we assume programs are aligned to bank boundaries?
	 * @todo Do we need the least-issue delay, or can we use the true
	 * last-data arrival time here and assume the pipeline doesn't issue a
	 * request before LID passed?
	 */
	uint32_t
	read_DDR4_cycles(size_t bytes)
	{
		size_t b;
		uint32_t dram_cycles;
		const dram_timing *t;

		t = getTiming(MC_DRAM_SPEED, MC_DRAM_ORG, MC_DRAM_BANKS / 4);

		b = bursts(t, bytes, 1);
		dram_cycles = least_issue_delay_rd_ddr4(t, b, 1);

		/* And convert to SimdCluster cycles
		 * @todo Less static */

		return ((dram_cycles * 1000) + (t->clkMHz-1)) / t->clkMHz;
	}

	/**
	 * A separate thread for maintaining the cycle counter.
	 *
	 * This way we can block in thread_lt() on fifo writes without worries.
	 */
	void
	thread_cycle_counter(void)
	{
		while (true) {
			wait();
			wait(SC_ZERO_TIME);
			cycle++;
		}
	}

	/** Perform a reset */
	void
	do_rst(void)
	{
		state = WS_STATE_IDLE;
		out_imem_w.write(false);
		out_xlat_w.write(false);
	}

	/** Main thread. */
	void
	thread_lt(void)
	{
		work<XLAT_ENTRIES> work;
		/* X is in units of 32 threads */
		unsigned int x, y, pc;
		workgroup<THREADS,LANES> wg;
		vector<Instruction>::iterator it;
		unsigned int i, j;
		unsigned long cycle_fini_upload;

		do_rst();

		while (true) {
			switch (state) {
			case WS_STATE_IDLE:
				out_end_prg.write(false);

				if (!in_kick.read())
					break;

				start_cycle = cycle;

				work = in_work.read();

				/* If there's no kernel, nothing to do */
				if (work.imem.size() == 0)
					break;

				state = WS_STATE_LOAD_KERNEL;

				stats.prg_load_time = read_DDR4_cycles(work.imem.size() * opcode_bytes);
				cycle_fini_upload = cycle + stats.prg_load_time;

				it = work.imem.begin();

				/* X is in units of 32 threads */
				x = 0;
				y = 0;
				pc = 0;
				i = 0;

				assert((32 << work.wg_width) <= THREADS);

				wg.last_warp = (THREADS/LANES) - 1; /** @todo */
				out_wg_width.write(work.wg_width);
				out_sched_opts.write(work.ws_sched);
				out_dim[0].write(work.dims[0]);
				out_dim[1].write(work.dims[1]);

				cout << "*************** Kernel kick-off. "
					"Global dim (" << work.dims[0] << "," <<
					work.dims[1] << ") Local dim (" <<
					(32 << work.wg_width) << "," <<
					(THREADS/(32 << work.wg_width)) << ")" <<
					" ***************" << endl;
				/* fall-through */
			case WS_STATE_LOAD_KERNEL:
				if (it != work.imem.end()) {
					for (j = 0; j < 4; j++) {
						if (it != work.imem.end())
							out_imem_op[j].write(*(it++));
						else
							out_imem_op[j].write(Instruction());
					}
					out_imem_pc.write(pc);
					out_imem_w.write(true);

					pc += 4;
				} else {
					out_imem_w.write(false);
				}

				if (i < work.bufs) {
					out_xlat_idx_w.write(i);
					out_xlat_phys_w.write(work.buf_map[i]);
					out_xlat_w.write(true);
				} else {
					out_xlat_w.write(false);
				}

				if (i < work.sp_bufs) {
					out_sp_xlat_idx_w.write(i);
					out_sp_xlat_phys_w.write(work.sp_buf_map[i]);
					out_sp_xlat_w.write(true);
				} else {
					out_sp_xlat_w.write(false);
				}
				i++;

				if (cycle < cycle_fini_upload)
					break;

				state = WS_STATE_ENUM_WGS;

				/* fall-through */
			case WS_STATE_ENUM_WGS:
				wg.off_x = x;
				wg.off_y = y;
				out_wg.write(wg);

				stats.threads += LANES * (wg.last_warp + 1);
				stats.wgs++;

				x += (1 << work.wg_width);
				if ((x << 5) >= work.dims[0]) {
					x = 0;
					y += (THREADS >> (work.wg_width + 5));
				}

				if (y >= work.dims[1]) {
					out_end_prg.write(true);
					state = WS_STATE_WAIT_FINI;
				}

				break;
			case WS_STATE_WAIT_FINI:
				if (in_exec_fini.read()) {
					state = WS_STATE_IDLE;
					stats_set_cycle_time();
				}
				break;
			default:
				assert(false);
			}

			wait();
		}
	}
};

}

#endif /* COMPUTE_CONTROL_WORKSCHEDULER_H */
