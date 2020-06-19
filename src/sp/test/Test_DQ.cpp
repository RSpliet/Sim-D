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

#include "util/SimdTest.h"
#include "sp/model/DQ_reservation.h"
#include "sp/control/DQ.h"

using namespace sc_core;
using namespace sc_dt;
using namespace std;
using namespace simd_test;
using namespace sp_model;

namespace sp_test {

/** Unit test for the scratchpad data path scheduler.
 * @param BUS_WIDTH Number of (32-bit) words transferred per cycle.
 * @param THREADS Number of threads in a work-group.
 */
template <unsigned int BUS_WIDTH, unsigned int THREADS>
class Test_DQ : public SimdTest
{
public:
	/** DRAM clock, SDR. */
	sc_in<bool> in_clk{"in_clk"};

	/** True if a read-operation (SP->RF) is taking place. */
	sc_inout<bool> out_read{"out_read"};

	/** Stride sequencer output. */
	sc_fifo_out<DQ_reservation<BUS_WIDTH,THREADS> >
			out_dq_fifo{"out_dq_fifo"};

	/******************** Register file interface *****************/
	/** Operation targets DRAM (enable bit). */
	sc_in<bool> in_rf_rw{"in_rf_rw"};

	/** Write mask. */
	sc_in<sc_bv<BUS_WIDTH> > in_rf_mask_w{"out_dram_mask_w"};

	/** Indexes for each incoming data word */
	sc_in<reg_offset_t<THREADS> > in_rf_idx_w[BUS_WIDTH];

	/** Signal completion of a DQ reservation with the "last" bool set. */
	sc_in<bool> in_done{"in_done"};

	/******************** StorageArray interface *****************/
	/** StorageArray RF signal bundle. */
	sc_in<dq_pipe_sa<BUS_WIDTH> > in_sa_cmd{"in_sa_cmd"};

	SC_CTOR(Test_DQ)
	{
		SC_THREAD(thread);
		sensitive << in_clk.pos();
	}

private:
	void
	test_read(void)
	{
		DQ_reservation<BUS_WIDTH,THREADS> res;
		dq_pipe_sa<BUS_WIDTH> psa;
		unsigned int i;

		res.rw = true;
		res.sp_offset = 0x4;
		res.wordmask = 0xf;
		res.write = true;
		for (i = 0; i < BUS_WIDTH; i++) {
			res.reg_offset[i].row = 0;
			res.reg_offset[i].lane = i;
		}

		/* This is mainly an exercise in timing */
		out_read.write(true);
		out_dq_fifo.write(res);
		wait(SC_ZERO_TIME);

		wait();

		/* Otra vez. */
		res.sp_offset = 0xc;
		res.last = true;
		out_dq_fifo.write(res);
		wait(SC_ZERO_TIME);

		psa = in_sa_cmd.read();
		assert(psa.rw == true);
		assert(psa.addr == 0x4);
		assert(psa.mask_w == 0xf);

		assert(in_rf_rw.read() == false);

		assert(!in_done.read());

		wait();

		psa = in_sa_cmd.read();
		assert(psa.rw == true);
		assert(psa.addr == 0xc);
		assert(psa.mask_w == 0xf);

		assert(in_rf_rw.read() == true);
		for (i = 0; i < BUS_WIDTH; i++) {
			assert(in_rf_idx_w[i].read().row == 0);
			assert(in_rf_idx_w[i].read().lane == i);
		}

		assert(!in_done.read());

		wait();
		out_read.write(false);
		psa = in_sa_cmd.read();
		assert(psa.rw == false);
		assert(in_done.read());

		assert(in_rf_rw.read() == true);
		for (i = 0; i < BUS_WIDTH; i++) {
			assert(in_rf_idx_w[i].read().row == 0);
			assert(in_rf_idx_w[i].read().lane == i);
		}

		wait();
		assert(!in_done.read());
	}

	void
	test_write(void)
	{
		DQ_reservation<BUS_WIDTH,THREADS> res;
		dq_pipe_sa<BUS_WIDTH> psa;
		unsigned int i;

		res.rw = true;
		res.sp_offset = 0x14;
		res.wordmask = 0xf;
		res.write = false;
		for (i = 0; i < BUS_WIDTH; i++) {
			res.reg_offset[i].row = 0;
			res.reg_offset[i].lane = i;
		}

		/* This is mainly an exercise in timing */
		out_read.write(false);
		out_dq_fifo.write(res);
		wait(SC_ZERO_TIME);

		wait();

		/* Opnieuw. */
		res.last = true;
		out_dq_fifo.write(res);

		psa = in_sa_cmd.read();

		assert(psa.rw == false);
		assert(in_rf_rw.read() == true);
		for (i = 0; i < BUS_WIDTH; i++) {
			assert(in_rf_idx_w[i].read().row == 0);
			assert(in_rf_idx_w[i].read().lane == i);
		}

		assert(!in_done.read());

		wait();
		psa = in_sa_cmd.read();
		assert(psa.rw == true);
		assert(psa.addr == 0x14);
		assert(psa.mask_w == 0xf);

		assert(in_rf_rw.read() == true);
		for (i = 0; i < BUS_WIDTH; i++) {
			assert(in_rf_idx_w[i].read().row == 0);
			assert(in_rf_idx_w[i].read().lane == i);
		}
		assert(!in_done.read());

		wait();
		psa = in_sa_cmd.read();
		assert(psa.rw == true);
		assert(psa.addr == 0x14);
		assert(psa.mask_w == 0xf);

		assert(in_rf_rw.read() == false);
		assert(in_done.read());

		wait();
		assert(!in_done.read());

	}

	void
	thread(void)
	{
		test_read();
		test_write();

		test_finish();
	}
};

}

using namespace sp_control;
using namespace sp_test;

int
main(int argc, char **argv)
{
	unsigned int i;

	DQ<4,1024> dq("dq");
	Test_DQ<4,1024> dq_test("dq_test");

	sc_clock clk("clk", sc_time(10./16., SC_NS));
	sc_signal<bool> read;
	sc_fifo<DQ_reservation<4,1024> > dq_res(1);
	sc_signal<sc_bv<4> > rf_mask_w;
	sc_signal<reg_offset_t<1024> > rf_idx_w[4];
	sc_signal<bool> rf_rw;
	sc_signal<bool> rf_done;
	sc_signal<dq_pipe_sa<4> > sa_cmd;

	dq.in_clk(clk);
	dq.in_read(read);
	dq.in_dq_fifo(dq_res);
	dq.out_rf_rw(rf_rw);
	dq.out_rf_mask_w(rf_mask_w);
	dq.out_done(rf_done);
	dq.out_sa_cmd(sa_cmd);

	dq_test.in_clk(clk);
	dq_test.out_read(read);
	dq_test.out_dq_fifo(dq_res);
	dq_test.in_rf_mask_w(rf_mask_w);
	dq_test.in_rf_rw(rf_rw);
	dq_test.in_done(rf_done);
	dq_test.in_sa_cmd(sa_cmd);

	for (i = 0; i < 4; i++) {
		dq.out_rf_idx_w[i](rf_idx_w[i]);
		dq_test.in_rf_idx_w[i](rf_idx_w[i]);
	}

	sc_start(400, SC_NS);

	assert(dq_test.has_finished());
}
