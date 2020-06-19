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

#ifndef COMPUTE_CONTROL_SCOREBOARD_H
#define COMPUTE_CONTROL_SCOREBOARD_H

#include "model/reg_read_req.h"
#include "util/constmath.h"
#include "util/debug_output.h"

using namespace sc_core;
using namespace sc_dt;
using namespace compute_model;
using namespace simd_model;

namespace compute_control {

/** A scoreboard tracking writes, identifies RAW hazards.
 *
 * Implementation wise, CPU-oriented scoreboards tend to just be a bitmap
 * marking the registers that are in used. However, we are managing vastly more
 * registers due to both heavy use of HW threading and in an attempt to avoid
 * the huge penalty for spilling. Since this is an in-order pipeline with
 * a handful of stages, we can save significantly on registers by using a
 * ringbuffer of CAMs.
 * These CAMs could live alongside each pipeline stage in a real HW
 * (systemverilog) implementation. We group all the CAMs in this one Scoreboard
 * module instead for code readability purposes.
 */
template <unsigned int THREADS = 1024, unsigned int LANES = 128>
class Scoreboard : public sc_core::sc_module
{
private:
	/** Number of slots in the scoreboard.
	 *
	 * This needs to be configurable at run-time, hence it's not a template
	 * argument. Because we must expose bit vectors (see entries_pop),
	 * we have to hard-code the number of entries to a maximum, here 32. */
	unsigned int scoreboard_entries;

	/** Request queue, ringbuffer */
	Register<THREADS/LANES> *request_queue;

	/** Head of request queue, first unpopulated element. */
	unsigned int head;

	/** Tail of request queue, bottom entry */
	unsigned int tail;

	/** Performance counter: Maximum observed number of active entries in
	 * the scoreboard. */
	unsigned int max_entries;

	/** Population (bit-mask) of active entries in the queue. */
	sc_bv<32> entries_pop[2];

	/** Counters for how many CSTACK writes are pending in the pipeline. */
	unsigned int cstack_writes_pending[2];

public:
	/** Compute clock. */
	sc_in<bool> in_clk{"in_clk"};

	/** Consume an entry? */
	sc_in<bool> in_dequeue{"in_dequeue"};

	/** Produce an entry. */
	sc_in<bool> in_enqueue{"in_enqueue"};

	/** Consume an entry? */
	sc_in<bool> in_dequeue_cstack_write{"in_dequeue_cstack_write"};

	/** WG to consume CSTACK entry from */
	sc_in<sc_uint<1> > in_dequeue_cstack_wg{"in_dequeue_cstack_wg"};

	/** Produce an entry. */
	sc_in<bool> in_enqueue_cstack_write{"in_enqueue_cstack_write"};

	/** For this WG. */
	sc_in<sc_uint<1> > in_enqueue_cstack_wg{"in_enqueue_cstack_wg"};

	/** Indicate whether CPOP should not be issued yet, as CPUSHes are still
	 * in progress. */
	sc_inout<bool> out_cpop_stall[2];

	/** Write request to add */
	sc_in<Register<THREADS/LANES> > in_req_w{"in_req_w"};

	/** Read requests to check */
	sc_fifo_in<reg_read_req<THREADS/LANES> > in_req_r{"in_req_r"};

	/** True iff for the first read request a match should be reported
	 * against *any* special purpose scalar register. This to cover the
	 * implicit stride descriptor registers read by DRAM read/write
	 * operations */
	sc_in<bool> in_ssp_match{"in_ssp_match"};

	/** Scoreboard population to check each request against.
	 *
	 * Permits disabling certain CAMs such that multi-stage operand
	 * fetch doesn't match operands against itself.  The scoreboard will AND
	 * this value against the locally stored entries_pop to make sure only
	 * active entries. For single-cycle operand fetch, it's valid to
	 * hard-wire these entries to all-1.
	 */
	sc_in<sc_bv<32> > in_req_sb_pop[3];

	/** Should decode stall? */
	sc_fifo_out<sc_bv<3> > out_raw{"out_raw"};

	/** Read requests to test */

	/** Overflow/underrun warning */
	sc_inout<bool> out_ex_overflow{"out_ex_overflow"};

	/** Populated entries.
	 * These can be used by a pipelines operand fetch to provide a mask
	 * for tests.
	 * XXX: The length of this bit vector isn't tailored to the number of
	 * slots. Would be painful to achieve. Obviously in a real
	 * implementation we wouldn't need to waste bits.
	 */
	sc_inout<sc_bv<32> > out_entries_pop[2];

	/** Invalidate entries for given work-group. This does not pop them
	 * off, but avoids delays caused by false matches upon pipeline
	 * invalidation. */
	sc_in<bool> in_entries_disable{"in_entries_disable"};

	/** Workgroup for which entries should be disabled. */
	sc_in<sc_uint<1> > in_entries_disable_wg{"in_entries_disable_wg"};

	/** Constructor. */
	SC_CTOR(Scoreboard)
	: scoreboard_entries(8), head(0), tail(0), max_entries(0)
	{
		request_queue = new Register<THREADS/LANES>[scoreboard_entries];

		entries_pop[0] = 0;
		entries_pop[1] = 0;

		cstack_writes_pending[0] = 0;
		cstack_writes_pending[1] = 0;

		SC_THREAD(thread_push_pop);
		sensitive << in_clk.pos();

		SC_THREAD(thread_check);
		sensitive << in_clk.pos();
	}

	/** Destructor. */
	~Scoreboard(void)
	{
		if (request_queue) {
			delete[] request_queue;
			request_queue = nullptr;
		}
	}

	/** Return the maximum number of entries in the scoreboard.
	 * @return The capacity of the scoreboard.
	 */
	unsigned int
	get_max_entries(void)
	{
		return max_entries;
	}

	/** Set the number of scoreboard entries.
	 *
	 * The number should be equal to the number of IDecode+IExecute
	 * pipeline stages plus 1. Maximum of 32.
	 * @param entries Number of scoreboard entries.
	 */
	void
	set_slots(unsigned int entries)
	{
		if (entries > 32)
			throw invalid_argument("Scoreboard does not support "
					"more than 32 entries");

		if (request_queue) {
			delete[] request_queue;
			request_queue = nullptr;
		}

		request_queue = new Register<THREADS/LANES>[entries];
		scoreboard_entries = entries;
	}

	/** Debug: test whether the scoreboard contains a register
	 * @param reg Reg to match in the scoreboard. */
	bool
	debug_contains(Register<THREADS/LANES> &reg)
	{
		sc_bv<32> test_pop;
		unsigned int e;
		Register<THREADS/LANES> wreq;
		unsigned int i;

		for (i = 0; i < 3; i++) {
			for (e = tail; e != head;
			     e = (e + 1) % scoreboard_entries) {

				wreq = request_queue[e];

				if (reg == wreq)
					return true;
			}
		}

		return false;
	}

private:
	/** Return the number of entries currently in the scoreboard.
	 * @return Number of entries. */
	unsigned int
	entries(void)
	{
		unsigned int h = head;
		if (head < tail)
			h += scoreboard_entries;

		return h - tail;
	}

	/**
	 * Popper/pusher thread.
	 *
	 * Will pop first thing, will push *after* checking the incoming
	 * request.
	 */
	void
	thread_push_pop(void)
	{
		unsigned int e;
		unsigned int wg;
		out_entries_pop[0].write(0);
		out_entries_pop[1].write(0);

		while (true) {
			wait();
			out_ex_overflow.write(false);

			if (in_entries_disable.read())
				entries_pop[in_entries_disable_wg.read()] = 0;

			if (in_dequeue.read()) {
				if (head == tail) {
					/* @todo This really shouldn't happen.
					 * assert()? */
					out_ex_overflow.write(true);
					cerr << sc_time_stamp() <<
						" Popping from an empty SB" << endl;
					assert("Popping from an empty SB");
				} else {
					entries_pop[0][tail] = Log_0;
					entries_pop[1][tail] = Log_0;
					tail = (tail + 1) % scoreboard_entries;
				}
			}

			if (in_dequeue_cstack_write.read())
				cstack_writes_pending[in_dequeue_cstack_wg.read()]--;

			if (in_enqueue.read()) {
				if (head == min(tail - 1,
						scoreboard_entries - 1)) {
					out_ex_overflow.write(true);
					cerr << sc_time_stamp() <<
						" Pushing to a full SB" << endl;
				} else {
					request_queue[head] = in_req_w.read();
					wg = request_queue[head].wg;
					entries_pop[wg][head] = Log_1;
					head = (head + 1) % scoreboard_entries;
				}
			}

			if (in_enqueue_cstack_write.read())
				cstack_writes_pending[in_enqueue_cstack_wg.read()]++;

			max_entries = max(max_entries, entries());
			out_entries_pop[0].write(entries_pop[0]);
			out_entries_pop[1].write(entries_pop[1]);

			out_cpop_stall[0].write(cstack_writes_pending[0] > 0);
			out_cpop_stall[1].write(cstack_writes_pending[1] > 0);

			if (debug_output[DEBUG_COMPUTE_TRACE]) {
				cout << sc_time_stamp() << " Scoreboard: " <<
					entries() << " entries: ";

				for (e = tail; e != head;
				     e = (e + 1) % scoreboard_entries)
					cout << "(" << e <<
					(entries_pop[request_queue[e].wg][e] == Log_1 ? " " : " X")
					<< ") " << request_queue[e] << ", ";

				cout << endl;

				cout << sc_time_stamp() << " Scoreboard: (" <<
					cstack_writes_pending[0] << "," <<
					cstack_writes_pending[1] <<
					") Control stack writes pending." << endl;
			}
		}
	}

	/**
	 * The checker thread.
	 *
	 * Will assert out_stall_d if a RAW hazard is detected.
	 */
	void
	thread_check(void)
	{
		reg_read_req<THREADS/LANES> req;
		Register<THREADS/LANES> wreq;
		unsigned int i;
		unsigned int e;
		sc_bv<3> stall;
		sc_bv<32> test_pop;
		bool ssp_match;

		while (true) {
			wait();
			req = in_req_r.read();
			stall = 0;

			/* Short circuit if no entries in the scoreboard */
			if (head == tail) {
				out_raw.write(stall);
				continue;
			}

			ssp_match = in_ssp_match.read();

			for (i = 0; i < 3; i++) {
				if (!req.r[i])
					continue;

				test_pop = entries_pop[req.reg[i].wg] & in_req_sb_pop[i].read();

				for (e = tail; e != head;
				     e = (e + 1) % scoreboard_entries) {
					if (!test_pop[e])
						continue;

					wreq = request_queue[e];

					if (req.reg[i] == wreq)
						stall[i] = Log_1;

					if (ssp_match &&
					    wreq.type == REGISTER_SSP)
						stall[i] = Log_1;
				}

				ssp_match = false;
			}

			out_raw.write(stall);
		}
	}
};

}

#endif /* COMPUTE_CONTROL_SCOREBOARD_H */
