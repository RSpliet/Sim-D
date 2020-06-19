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

#include "compute/control/IFetch.h"
#include "compute/model/work.h"
#include "compute/model/imem_request.h"
#include "util/SimdTest.h"

using namespace sc_core;
using namespace sc_dt;
using namespace compute_control;
using namespace compute_model;
using namespace simd_test;

namespace compute_test {

/** Unit test for compute_control::IFetch */
template <unsigned int PC_WIDTH>
class Test_IFetch : public SimdTest
{
public:
	/** Compute clock. */
	sc_in<bool> in_clk{"in_clk"};

	/** Stall signal from decode */
	sc_inout<bool> out_stall_d{"out_stall_d"};

	/** State of the workgroups for this cluster */
	sc_inout<workgroup_state> out_wg_state[2];

	/** Finished bit, comes slightly earlier than state. */
	sc_inout<sc_bv<2> > out_wg_finished{"out_wg_finished"};

	/** Boolean, true iff PC should be overwritten (branch) */
	sc_inout<bool> out_pc_write{"out_pc_write"};

	/** PC to overwrite current PC with. */
	sc_inout<sc_uint<PC_WIDTH> > out_pc_w{"out_pc_w"};

	/** WG to overwrite PC for. */
	sc_inout<sc_uint<1> > out_pc_wg_w{"out_pc_wg_w"};

	/** Out PC, connecting to IMem input. */
	sc_fifo_in<imem_request<PC_WIDTH> > in_insn_r{"in_insn_r"};

	/** Currently active workgroup */
	sc_in<sc_uint<1> > in_wg{"in_wg"};

	/** Workgroup to reset PC for. */
	sc_inout<sc_uint<1> > out_pc_rst_wg{"pc_rst_wg"};

	/** Boolean: true iff PC for wg in in_pc_rst_wg must be reset. */
	sc_inout<bool> out_pc_rst{"pc_rst"};

	/** When true, don't schedule compute in parallel with SP read/write. */
	sc_inout<sc_bv<WSS_SENTINEL> > out_sched_opts{"out_sched_opts"};

	/** Construct test thread */
	SC_CTOR(Test_IFetch)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();
	}
private:
	/** Main thread */
	void
	thread_lt(void)
	{
		sc_bv<2> wg_finished;
		imem_request<PC_WIDTH> req;

		int i;

		out_pc_write.write(false);
		out_stall_d.write(false);
		out_sched_opts.write(0);
		out_wg_state[0].write(WG_STATE_RUN);
		out_wg_state[1].write(WG_STATE_RUN);
		wg_finished = 0;
		out_wg_finished.write(wg_finished);

		for (i = 0; i < 10; i++) {
			wait();
			req = in_insn_r.read();
			std::cout << req.pc << std::endl;
			assert(req.pc == i);
		}

		out_pc_write.write(true);
		out_pc_w.write(24);
		wait();
		req = in_insn_r.read();
		assert(req.pc == 24);
		out_pc_write.write(false);
		wait();
		req = in_insn_r.read();
		cout << req.pc << endl;
		assert(req.pc == 25);
		wait();
		req = in_insn_r.read();
		cout << req.pc << endl;
		assert(req.pc == 26);
		out_stall_d.write(true);
		wait();
		assert(in_insn_r.num_available() == 0);
		wait();
		assert(in_insn_r.num_available() == 0);
		out_stall_d.write(false);
		wait();
		req = in_insn_r.read();
		cout << req.pc << endl;
		assert(req.pc == 27);
		test_finish();
	}
};

}

using namespace compute_control;
using namespace compute_test;

int
sc_main(int argc, char* argv[])
{
	sc_signal<bool> pc_write;
	sc_signal<bool> stall_d;
	sc_signal<sc_uint<11> > pc_w;
	sc_fifo<imem_request<11> > insn_r(1);
	sc_signal<sc_uint<1> > wg;
	sc_signal<sc_uint<1> > pc_wg_w;
	sc_signal<sc_uint<1> > i_pc_wg_w;
	sc_signal<workgroup_state> wg_state[2];
	sc_signal<sc_bv<2> > wg_finished;
	sc_signal<sc_uint<1> > pc_rst_wg;
	sc_signal<bool> pc_rst;
	sc_signal<sc_bv<WSS_SENTINEL> > sched_opts;

	sc_clock clk("clk", sc_time(10./12., SC_NS));

	IFetch<11> my_ifetch("my_ifetch");
	my_ifetch.in_clk(clk);
	my_ifetch.in_stall_d(stall_d);
	my_ifetch.in_wg_state[0](wg_state[0]);
	my_ifetch.in_wg_state[1](wg_state[1]);
	my_ifetch.in_wg_finished(wg_finished);
	my_ifetch.in_pc_write(pc_write);
	my_ifetch.in_pc_w(pc_w);
	my_ifetch.in_pc_wg_w(pc_wg_w);
	my_ifetch.out_insn_r(insn_r);
	my_ifetch.out_wg(wg);
	my_ifetch.in_pc_rst_wg(pc_rst_wg);
	my_ifetch.in_pc_rst(pc_rst);
	my_ifetch.in_sched_opts(sched_opts);

	Test_IFetch<11> my_ifetch_test("my_ifetch_test");
	my_ifetch_test.in_clk(clk);
	my_ifetch_test.out_stall_d(stall_d);
	my_ifetch_test.out_wg_state[0](wg_state[0]);
	my_ifetch_test.out_wg_state[1](wg_state[1]);
	my_ifetch_test.out_wg_finished(wg_finished);
	my_ifetch_test.out_pc_write(pc_write);
	my_ifetch_test.out_pc_w(pc_w);
	my_ifetch_test.out_pc_wg_w(pc_wg_w);
	my_ifetch_test.in_insn_r(insn_r);
	my_ifetch_test.in_wg(wg);
	my_ifetch_test.out_pc_rst_wg(pc_rst_wg);
	my_ifetch_test.out_pc_rst(pc_rst);
	my_ifetch_test.out_sched_opts(sched_opts);

	sc_core::sc_start(300, sc_core::SC_NS);

	assert(my_ifetch_test.has_finished());

	return 0;
}
