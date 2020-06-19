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

#include <systemc>

#include "compute/control/Scoreboard.h"
#include "util/SimdTest.h"
#include "util/defaults.h"

using namespace sc_core;
using namespace sc_dt;
using namespace compute_control;
using namespace std;
using namespace simd_test;

namespace compute_test {

template <unsigned int THREADS, unsigned int FPUS>
class Test_Scoreboard : public SimdTest
{
private:
	unsigned int scoreboard_entries;

public:
	/** Compute clock. */
	sc_in<bool> in_clk{"in_clk"};

	/** Consume an entry ? */
	sc_inout<bool> out_dequeue{"out_dequeue"};

	/** Produce an entry */
	sc_inout<bool> out_enqueue{"out_enqueue"};

	/** Consume a CSTACK entry? */
	sc_inout<bool> out_dequeue_cstack_write{"out_dequeue_cstack_write"};

	/** WG to consume CSTACK entry from. */
	sc_inout<sc_uint<1> > out_dequeue_cstack_wg{"out_dequeue_cstack_wg"};

	/** Add a CSTACK write entry. */
	sc_in<bool> out_enqueue_cstack_write{"out_enqueue_cstack_write"};

	/** WG to add CSTACK write entry for. */
	sc_in<sc_uint<1> > out_enqueue_cstack_wg{"out_enqueue_cstack_wg"};

	/** Wait with issuing a CPOP, there's CSTACK writes in progress. */
	sc_in<bool> in_cpop_stall[2];

	/** Write request to add */
	sc_inout<Register<THREADS/FPUS> > out_req_w{"out_req_w"};

	/** Read requests to check */
	sc_fifo_out<reg_read_req<THREADS/FPUS> > out_req_r{"out_req_r"};

	/** True iff for the first read request a match should be reported
	 * against *any* special purpose scalar register. This to cover the
	 * implicit stride descriptor registers read by DRAM read/write
	 * operations */
	sc_inout<bool> out_ssp_match{"out_ssp_match"};

	sc_inout<sc_bv<32> > out_req_sb_pop[3];

	/** Should decode stall? */
	sc_fifo_in<sc_bv<3> > in_raw{"in_raw"};

	/** Overflow/underrun warning */
	sc_in<bool> in_ex_overflow{"in_ex_overflow"};

	sc_in<sc_bv<32> > in_entries_pop[2];

	/** Invalidate entries for given work-group. This does not pop them
	 * off, but avoids delays caused by false matches upon pipeline
	 * invalidation. */
	sc_inout<bool> out_entries_disable{"out_entries_disable"};

	/** Workgroup for which entries should be disabled. */
	sc_inout<sc_uint<1> > out_entries_disable_wg{"out_entries_disable_wg"};

	/** Constructor */
	SC_CTOR(Test_Scoreboard) : scoreboard_entries(8)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();
	}

private:
	void
	thread_lt(void)
	{
		reg_read_req<THREADS/FPUS> req;
		Register<THREADS/FPUS> reg;
		unsigned int i;
		sc_bv<32> rpop;
		sc_bv<32> pop = 0;
		pop.b_not();

		out_req_sb_pop[0].write(pop);
		out_req_sb_pop[1].write(pop);
		out_req_sb_pop[2].write(pop);

		/* It should be empty */
		out_dequeue.write(true);
		wait();
		wait(SC_ZERO_TIME);

		assert(in_ex_overflow.read());
		out_dequeue.write(false);
		wait();

		/* Let's add an entry. */
		reg = Register<THREADS/FPUS>(0,REGISTER_VGPR,3,0);
		out_req_w.write(reg);
		out_enqueue.write(true);

		/* Same cycle. This entry should not cause a match */
		req.r[0] = Log_1;
		req.reg[0] = reg;
		out_req_r.write(req);

		wait();

		pop.b_not();
		rpop = in_entries_pop[0].read();
		assert(pop == rpop);

		/* Test for the same register again. Now we should match */
		out_enqueue.write(false);
		assert(!in_raw.read().or_reduce());

		out_req_r.write(req);

		wait();
		assert(in_raw.read().or_reduce());

		pop[0] = Log_1;
		rpop = in_entries_pop[0].read();
		assert(pop == rpop);

		/* Test for a different register. Different types, whatnot */
		req.r[1] = Log_1;
		req.r[2] = Log_1;
		req.reg[0] = Register<THREADS/FPUS>(0,REGISTER_VGPR,2,0);
		req.reg[1] = Register<THREADS/FPUS>(0,REGISTER_SGPR,3,0);
		req.reg[2] = Register<THREADS/FPUS>(0,REGISTER_PR,3,0);
		out_req_r.write(req);

		wait();
		assert(!in_raw.read().or_reduce());

		/* And match the original register again */
		req.reg[2] = reg;
		out_req_r.write(req);

		wait();
		assert(in_raw.read().or_reduce());

		/* Let's start dequeueing. One entry so far. */
		out_dequeue.write(true);
		wait();
		/* Zero entries, still dequeuing */
		assert(!in_ex_overflow.read());

		wait();
		assert(!in_ex_overflow.read());
		pop[0] = Log_0;
		rpop = in_entries_pop[0].read();
		assert(pop == rpop);
		/* in_ex_overflow should be set now. Propagation delay means
		 * it arrives next cycle */
		wait();
		assert(in_ex_overflow.read());

		out_dequeue.write(false);
		wait();

		out_req_w.write(reg);
		out_enqueue.write(true);
		/* Let's fill her up to the brim */
		for (i = 0; i < scoreboard_entries - 1; i++) {
			wait();
			assert(!in_ex_overflow.read());
			rpop = in_entries_pop[0].read();
			assert(pop == rpop);
			pop[i+1 % scoreboard_entries] = Log_1;
		}
		wait();
		out_enqueue.write(false);
		/* There's a delay in the overflow signal. */
		wait();
		assert(in_ex_overflow.read());
	}
};

}

using namespace compute_test;

int
main(int argc, char **argv)
{
	Scoreboard<COMPUTE_THREADS,COMPUTE_FPUS> my_sb("my_sb");
	Test_Scoreboard<COMPUTE_THREADS,COMPUTE_FPUS> my_sb_test("my_sb_test");

	sc_clock clk("clk", sc_time(10./12., SC_NS));
	sc_signal<bool> dequeue;
	sc_signal<bool> enqueue;
	sc_signal<bool> dequeue_cstack_write;
	sc_signal<sc_uint<1> > dequeue_cstack_wg;
	sc_signal<bool> enqueue_cstack_write;
	sc_signal<sc_uint<1> > enqueue_cstack_wg;
	sc_signal<bool> cpop_stall[2];
	sc_signal<Register<COMPUTE_THREADS/COMPUTE_FPUS> > req_w;
	sc_fifo<reg_read_req<COMPUTE_THREADS/COMPUTE_FPUS> > req_r(1);
	sc_signal<bool> ssp_match;
	sc_fifo<sc_bv<3> > raw(1);
	sc_signal<bool> ex_overflow;
	sc_signal<sc_bv<32> > req_sb_pop[3];
	sc_signal<sc_bv<32> > entries_pop[2];
	sc_signal<bool> entries_disable;
	sc_signal<sc_uint<1> > entries_disable_wg;

	my_sb.in_clk(clk);
	my_sb.in_dequeue(dequeue);
	my_sb.in_enqueue(enqueue);
	my_sb.in_dequeue_cstack_write(dequeue_cstack_write);
	my_sb.in_dequeue_cstack_wg(dequeue_cstack_wg);
	my_sb.in_enqueue_cstack_write(enqueue_cstack_write);
	my_sb.in_enqueue_cstack_wg(enqueue_cstack_wg);
	my_sb.out_cpop_stall[0](cpop_stall[0]);
	my_sb.out_cpop_stall[1](cpop_stall[1]);
	my_sb.in_req_w(req_w);
	my_sb.in_req_r(req_r);
	my_sb.in_ssp_match(ssp_match);
	my_sb.in_req_sb_pop[0](req_sb_pop[0]);
	my_sb.in_req_sb_pop[1](req_sb_pop[1]);
	my_sb.in_req_sb_pop[2](req_sb_pop[2]);
	my_sb.out_raw(raw);
	my_sb.out_ex_overflow(ex_overflow);
	my_sb.out_entries_pop[0](entries_pop[0]);
	my_sb.out_entries_pop[1](entries_pop[1]);
	my_sb.in_entries_disable(entries_disable);
	my_sb.in_entries_disable_wg(entries_disable_wg);

	my_sb.set_slots(8);

	my_sb_test.in_clk(clk);
	my_sb_test.out_dequeue(dequeue);
	my_sb_test.out_enqueue(enqueue);
	my_sb_test.out_dequeue_cstack_write(dequeue_cstack_write);
	my_sb_test.out_dequeue_cstack_wg(dequeue_cstack_wg);
	my_sb_test.out_enqueue_cstack_write(enqueue_cstack_write);
	my_sb_test.out_enqueue_cstack_wg(enqueue_cstack_wg);
	my_sb_test.in_cpop_stall[0](cpop_stall[0]);
	my_sb_test.in_cpop_stall[1](cpop_stall[1]);
	my_sb_test.out_req_w(req_w);
	my_sb_test.out_req_r(req_r);
	my_sb_test.out_ssp_match(ssp_match);
	my_sb_test.out_req_sb_pop[0](req_sb_pop[0]);
	my_sb_test.out_req_sb_pop[1](req_sb_pop[1]);
	my_sb_test.out_req_sb_pop[2](req_sb_pop[2]);
	my_sb_test.in_raw(raw);
	my_sb_test.in_ex_overflow(ex_overflow);
	my_sb_test.in_entries_pop[0](entries_pop[0]);
	my_sb_test.in_entries_pop[1](entries_pop[1]);
	my_sb_test.out_entries_disable(entries_disable);
	my_sb_test.out_entries_disable_wg(entries_disable_wg);

	sc_start(700, SC_NS);

	return 0;
}
