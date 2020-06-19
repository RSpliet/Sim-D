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

#include "compute/control/WorkScheduler.h"
#include "isa/model/Instruction.h"
#include "model/Buffer.h"
#include "util/SimdTest.h"
#include "util/defaults.h"

using namespace sc_core;
using namespace sc_dt;
using namespace std;
using namespace compute_control;
using namespace compute_model;
using namespace simd_test;

namespace compute_test{

template <unsigned int THREADS = 1024, unsigned int FPUS = 128,
		unsigned int PC_WIDTH = 11, unsigned int XLAT_ENTRIES = 32>
class Test_WorkScheduler : public SimdTest
{
public:
	/** Compute clock. */
	sc_in<bool> in_clk{"in_clk"};

	/** Work specification. */
	sc_inout<work<XLAT_ENTRIES> > out_work{"out_work"};

	/** Kick off work. */
	sc_inout<bool> out_kick{"out_kick"};

	/** Workgroup generated
	 * @todo FIFO? */
	sc_fifo_in<workgroup<THREADS,FPUS> > in_wg{"in_wg"};

	/** Workgroup width. */
	sc_in<workgroup_width> in_wg_width{"in_wg_width"};

	/** Schedulign options. */
	sc_in<sc_bv<WSS_SENTINEL> > in_sched_opts{"in_sched_opts"};

	/** X,Y dimensions of work. */
	sc_in<sc_uint<32> > in_dim[2];

	/** Instruction to upload to IMem */
	sc_in<Instruction> in_imem_op[4];

	/** PC for instruction */
	sc_in<sc_uint<PC_WIDTH> > in_imem_pc{"in_imem_pc"};

	/** Write bit */
	sc_in<bool> in_imem_w{"in_imem_w"};

	/** True iff all workgroups have been enumerated into the FIFO. */
	sc_in<bool> in_end_prg{"out_end_prg"};

	/** True iff execution is finished. */
	sc_inout<bool> out_exec_fini{"in_exec_fini"};

	/** Write a translation table entry */
	sc_in<bool> in_xlat_w{"in_xlat_w"};

	/** Buffer index to write to. */
	sc_in<sc_uint<const_log2(XLAT_ENTRIES)> >
					in_xlat_idx_w{"in_xlat_idx_w"};

	/** Physical address indexed by buffer index. */
	sc_in<Buffer> in_xlat_phys_w{"in_xlat_phys_w"};

	/** Write a translation table entry */
	sc_in<bool> in_sp_xlat_w{"in_sp_xlat_w"};

	/** Buffer index to write to. */
	sc_in<sc_uint<const_log2(XLAT_ENTRIES)> >
					in_sp_xlat_idx_w{"in_sp_xlat_idx_w"};

	/** Physical address indexed by buffer index. */
	sc_in<Buffer> in_sp_xlat_phys_w{"in_sp_xlat_phys_w"};

	SC_CTOR(Test_WorkScheduler)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();
	}
private:
	void
	test_work(work<XLAT_ENTRIES> w)
	{
		unsigned int x, y;
		unsigned int i;
		workgroup<THREADS,FPUS> wg;

		w.add_op(Instruction());
		w.add_op(Instruction(OP_EXIT));
		out_work.write(w);
		out_kick.write(true);
		wait();

		out_kick.write(false);

		wait();

		assert(in_dim[0].read() == w.dims[0]);
		assert(in_dim[1].read() == w.dims[1]);
		assert(in_wg_width.read() == w.wg_width);

		/* First a program+buffer upload */
		for (i = 0; i < max(w.get_bufs(),1u); i++) {
			if (i == 0) {
				assert(in_imem_w.read() == true);
				assert(in_imem_pc.read() == 0);
				assert(in_imem_op[0].read() == Instruction());
			} else {
				assert(in_imem_w.read() == false);
			}

			if (i < w.get_bufs()) {
				assert(in_xlat_w.read() == true);
				assert(in_xlat_idx_w.read() == i);
				assert(in_xlat_phys_w.read() == w.get_buf(i));
			} else {
				assert(in_xlat_w.read() == false);
			}
			wait();
		}

		assert(in_imem_w.read() == false);

		for (y = 0; y < w.dims[1]; y += (THREADS >> (w.wg_width + 5))) {
			for (x = 0; x < w.dims[0]; x += (32 << w.wg_width)) {
				wg = in_wg.read();
				assert((wg.off_x << 5) == x);
				assert(wg.off_y == y);
				wait();
			}
		}
		assert(in_end_prg.read());
		out_exec_fini.write(true);
		wait();
		out_exec_fini.write(false);
		wait();
		wait();
		assert(!in_end_prg.read());
	}

	void
	thread_lt(void)
	{
		work<XLAT_ENTRIES> w;

		w = work<XLAT_ENTRIES>(165,34,WG_WIDTH_32);
		w.add_buf(Buffer(0x4000,1048576,1));
		w.add_buf(Buffer(0x14000,16,1));
		w.add_buf(Buffer(0x2654000,1048576,1));
		test_work(w);
		if (THREADS >= 1024)
			test_work(work<XLAT_ENTRIES>(1048576,1,WG_WIDTH_1024));
		if (THREADS >= 512)
			test_work(work<XLAT_ENTRIES>(1048576,1,WG_WIDTH_512));

		test_finish();
	}
};

}

using namespace compute_test;

int
main(int argc, char **argv)
{
	WorkScheduler<COMPUTE_THREADS,COMPUTE_FPUS,11,MC_BIND_BUFS> my_ws("my_ws");
	Test_WorkScheduler<COMPUTE_THREADS,COMPUTE_FPUS,11,MC_BIND_BUFS> my_ws_test("my_ws_test");

	sc_signal<work<MC_BIND_BUFS> > work;
	sc_signal<bool> kick;
	sc_fifo<workgroup<COMPUTE_THREADS,COMPUTE_FPUS> > wg(1);

	sc_signal<Instruction> imem_op[4];
	sc_signal<sc_uint<11> > imem_pc;
	sc_signal<bool> imem_w;

	sc_signal<sc_uint<32> > dim[2];
	sc_signal<workgroup_width> wg_width;
	sc_signal<sc_bv<WSS_SENTINEL> > sched_opts;
	sc_signal<bool> end_prg;
	sc_signal<bool> exec_fini;

	sc_signal<bool> xlat_w;
	sc_signal<sc_uint<const_log2(MC_BIND_BUFS)> > xlat_idx_w;
	sc_signal<Buffer> xlat_phys_w;

	sc_signal<bool> sp_xlat_w;
	sc_signal<sc_uint<const_log2(MC_BIND_BUFS)> > sp_xlat_idx_w;
	sc_signal<Buffer> sp_xlat_phys_w;
	unsigned int i;

	sc_clock clk("clk", sc_time(10./12., SC_NS));

	my_ws.in_clk(clk);
	my_ws.in_work(work);
	my_ws.in_kick(kick);
	my_ws.out_wg(wg);
	my_ws.out_wg_width(wg_width);
	my_ws.out_sched_opts(sched_opts);
	my_ws.out_dim[0](dim[0]);
	my_ws.out_dim[1](dim[1]);
	my_ws.out_imem_pc(imem_pc);
	my_ws.out_imem_w(imem_w);
	my_ws.out_end_prg(end_prg);
	my_ws.in_exec_fini(exec_fini);
	my_ws.out_xlat_w(xlat_w);
	my_ws.out_xlat_idx_w(xlat_idx_w);
	my_ws.out_xlat_phys_w(xlat_phys_w);
	my_ws.out_sp_xlat_w(sp_xlat_w);
	my_ws.out_sp_xlat_idx_w(sp_xlat_idx_w);
	my_ws.out_sp_xlat_phys_w(sp_xlat_phys_w);

	my_ws_test.in_clk(clk);
	my_ws_test.out_work(work);
	my_ws_test.out_kick(kick);
	my_ws_test.in_wg(wg);
	my_ws_test.in_wg_width(wg_width);
	my_ws_test.in_sched_opts(sched_opts);
	my_ws_test.in_dim[0](dim[0]);
	my_ws_test.in_dim[1](dim[1]);
	my_ws_test.in_imem_pc(imem_pc);
	my_ws_test.in_imem_w(imem_w);
	my_ws_test.in_end_prg(end_prg);
	my_ws_test.out_exec_fini(exec_fini);
	my_ws_test.in_xlat_w(xlat_w);
	my_ws_test.in_xlat_idx_w(xlat_idx_w);
	my_ws_test.in_xlat_phys_w(xlat_phys_w);
	my_ws_test.in_sp_xlat_w(sp_xlat_w);
	my_ws_test.in_sp_xlat_idx_w(sp_xlat_idx_w);
	my_ws_test.in_sp_xlat_phys_w(sp_xlat_phys_w);

	for (i = 0; i < 4; i++) {
		my_ws.out_imem_op[i](imem_op[i]);
		my_ws_test.in_imem_op[i](imem_op[i]);
	}

	sc_start(5000, SC_NS);
	assert(my_ws_test.has_finished());

	return 0;
}
