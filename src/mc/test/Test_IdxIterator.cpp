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

#include "mc/control/StrideSequencer.h"
#include "util/SimdTest.h"

using namespace sc_core;
using namespace sc_dt;
using namespace mc_model;
using namespace mc_control;
using namespace simd_test;

namespace mc_test {

static idx_t<COMPUTE_THREADS> idxs_ptrn_1[]{
		{0,0x000000},
		{0,0x000006},
		{0,0x000012},
		{0,0x000120},
		{0,0x000660},
		{0,0x000000},
		{0,0x000001},
		{}
};

/** Unit test for mc_control::StrideSequencer's index iterator functionality
 * XXX: merge with Test_StrideSequencer. */
template <unsigned int BUS_WIDTH, unsigned int THREADS>
class Test_IdxIterator : public SimdTest
{
public:
public:
	/** DRAM clock, SDR */
	sc_in<bool> in_clk{"in_clk"};

	/** FIFO of descriptors
	 * @todo Depth? */
	sc_fifo_out<stride_descriptor> out_desc_fifo{"out_desc_fifo"};

	/** Trigger a flush of the current request FIFO */
	sc_fifo_out<bool> out_trigger{"out_trigger"};

	/** Ref pending signal */
	sc_inout<bool> out_ref_pending{"out_ref_pending"};

	/** Generated request */
	sc_fifo_in<burst_request<BUS_WIDTH,THREADS> > in_req_fifo{"out_req_fifo"};

	/** Ready to accept next descriptor */
	sc_in<bool> in_done{"in_done"};

	/** Finished, all banks precharged */
	sc_inout<bool> out_DQ_allpre{"out_DQ_allpre"};

	/** Which destination is targeted by the currently active request? */
	sc_in<RequestTarget> in_dst{"in_dst"};

	/** Register addressed by DRAM, if any. */
	sc_in<AbstractRegister> in_dst_reg{"in_dst_reg"};

	/** Trigger the start of pushing indexes from RF. */
	sc_in<bool> in_idx_push_trigger{"in_idx_push_trigger"};

	/** RF will start pushing indexes for "index iteration" transfers. */
	sc_fifo_out<idx_t<THREADS> > out_idx{"out_idx"};

	/** Cycle counter. */
	sc_inout<long> out_cycle{"out_cycle"};

	/** Scheduling options. */
	sc_inout<sc_bv<WSS_SENTINEL> > out_sched_opts{"out_sched_opts"};

	/** Ticket number that's ready to pop. */
	sc_inout<sc_uint<4> > out_ticket_pop{"out_ticket_pop"};

	/** Construct test thread */
	SC_CTOR(Test_IdxIterator)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();

		SC_THREAD(thread_cycle);
		sensitive << in_clk.pos();
	}
private:
	/** Main thread */
	void
	thread_lt(void)
	{
		AbstractRegister t(0,REGISTER_VGPR,0);
		stride_descriptor idxdesc(t);
		burst_request<BUS_WIDTH,THREADS> req;
		unsigned int i;
		sc_uint<20> idx;
		sc_uint<32> addr;
		sc_uint<32> baddr;
		sc_bv<16> mask;
		sc_bv<WSS_SENTINEL> sched_opts;

		idxdesc.type = stride_descriptor::IDXIT;
		idxdesc.addr = 0x1000;
		idxdesc.write = false;
		idxdesc.dst_offset = 0;
		out_desc_fifo.write(idxdesc);

		sched_opts = 0;

		out_sched_opts.write(sched_opts);
		out_ticket_pop.write(0);

		for (i = 0; i < sizeof(idxs_ptrn_1)/sizeof(idxs_ptrn_1[0]);
							i++) {
			out_idx.write(idxs_ptrn_1[i]);
		}
		i = 0;

		out_ref_pending.write(1);
		out_trigger.write(1);
		wait();
		wait();
		wait();
		wait();
		wait();
		wait();
		assert(in_req_fifo.num_available() == 0);
		out_ref_pending.write(0);
		wait();

		do {
			while (in_req_fifo.num_available()) {
				in_req_fifo.read(req);

				addr = idxdesc.addr + (idxs_ptrn_1[i].dram_off << 2);
				baddr = addr & (~((BUS_WIDTH << 2)-1));
				mask = (1 << ((addr ^ baddr) >> 2));
				std::cout << std::hex << baddr << " " << mask << std::endl;
				assert(baddr == req.addr);
				assert(mask == req.wordmask);

				std::cout << req << std::endl;
				i++;

			}
			wait();
		} while (!in_done.read());

		wait();
		wait();
		wait();
		wait();
		wait();

		in_req_fifo.read(req);
		std::cout << req << std::endl;
	}

	void
	thread_cycle(void)
	{
		out_cycle.write(0);

		while (1) {
			wait();
			out_cycle.write(out_cycle.read() + 1);
		}
	}
};

}

using namespace mc_control;
using namespace mc_test;

int
sc_main(int argc, char* argv[])
{
	sc_clock clk("clk", sc_time(10./12., SC_NS));

	sc_fifo<stride_descriptor> desc_fifo("desc_fifo");
	sc_fifo<bool> trigger("trigger");
	sc_signal<bool> ref_pending;
	sc_fifo<burst_request<MC_BUS_WIDTH,COMPUTE_THREADS> >
					req_fifo("req_fifo");
	sc_signal<bool> done;
	sc_signal<bool> dq_allpre;
	sc_signal<RequestTarget> dst;
	sc_signal<AbstractRegister> dst_reg;
	sc_signal<bool> idx_push_trigger;
	sc_fifo<idx_t<COMPUTE_THREADS> > idx_fifo("idx_fifo");
	sc_signal<long> cycle;
	sc_signal<sc_bv<WSS_SENTINEL> > sched_opts;
	sc_signal<sc_uint<4> > ticket_pop;

	StrideSequencer<MC_BUS_WIDTH,COMPUTE_THREADS,128> my_cmdgen("my_cmdgen");
	my_cmdgen.in_clk(clk);
	my_cmdgen.in_desc_fifo(desc_fifo);
	my_cmdgen.in_trigger(trigger);
	my_cmdgen.in_ref_pending(ref_pending);
	my_cmdgen.out_req_fifo(req_fifo);
	my_cmdgen.out_done(done);
	my_cmdgen.in_DQ_allpre(dq_allpre);
	my_cmdgen.out_dst(dst);
	my_cmdgen.out_dst_reg(dst_reg);
	my_cmdgen.out_idx_push_trigger(idx_push_trigger);
	my_cmdgen.in_idx(idx_fifo);
	my_cmdgen.in_cycle(cycle);
	my_cmdgen.in_sched_opts(sched_opts);
	my_cmdgen.in_ticket_pop(ticket_pop);

	Test_IdxIterator<16,1024> my_cmdgen_test("my_cmdgen_test");
	my_cmdgen_test.in_clk(clk);
	my_cmdgen_test.out_desc_fifo(desc_fifo);
	my_cmdgen_test.out_trigger(trigger);
	my_cmdgen_test.out_ref_pending(ref_pending);
	my_cmdgen_test.in_req_fifo(req_fifo);
	my_cmdgen_test.in_done(done);
	my_cmdgen_test.out_DQ_allpre(dq_allpre);
	my_cmdgen_test.in_dst(dst);
	my_cmdgen_test.in_dst_reg(dst_reg);
	my_cmdgen_test.in_idx_push_trigger(idx_push_trigger);
	my_cmdgen_test.out_idx(idx_fifo);
	my_cmdgen_test.out_cycle(cycle);
	my_cmdgen_test.out_sched_opts(sched_opts);
	my_cmdgen_test.out_ticket_pop(ticket_pop);

	sc_core::sc_start(700, sc_core::SC_NS);

	return 0;
}
