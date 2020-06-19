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

#include <string>
#include <vector>

#include "mc/control/CmdArb_DDR4.h"
#include "util/defaults.h"
#include "util/SimdTest.h"

using namespace sc_core;
using namespace sc_dt;
using namespace mc_model;
using namespace mc_control;
using namespace simd_test;

namespace mc_test {

/** @internal Test pattern format */
typedef struct {
	sc_uint<20> bank;
	cmd_DDR<16,1024> rwp;
} test_ptrn;

/** @internal Test data */
static test_ptrn test_ptrn_1[]{
	{0, {.row = 10, .col = 0, .pre_pre = 0, .act = 1, .read = 1, .write = 0, .pre_post = 0, .sp_offset = 0x1000}},
	{1, {.row = 10, .col = 0, .pre_pre = 0, .act = 1, .read = 1, .write = 0, .pre_post = 0, .sp_offset = 0x1040}},
	{0, {.row = 10, .col = 8, .pre_pre = 0, .act = 0, .read = 1, .write = 0, .pre_post = 0}},
	{1, {.row = 10, .col = 8, .pre_pre = 0, .act = 0, .read = 1, .write = 0, .pre_post = 0}},
	{0, {.row = 10, .col = 16, .pre_pre = 0, .act = 0, .read = 1, .write = 0, .pre_post = 0}},
	{1, {.row = 10, .col = 16, .pre_pre = 0, .act = 0, .read = 1, .write = 0, .pre_post = 0}},
	{0, {.row = 10, .col = 24, .pre_pre = 0, .act = 0, .read = 1, .write = 0, .pre_post = 0}},
	{1, {.row = 10, .col = 24, .pre_pre = 0, .act = 0, .read = 1, .write = 0, .pre_post = 0}},
	{0, {.row = 10, .col = 32, .pre_pre = 0, .act = 0, .read = 1, .write = 0, .pre_post = 0}},
	{1, {.row = 10, .col = 32, .pre_pre = 0, .act = 0, .read = 1, .write = 0, .pre_post = 1}},
	{0, {.row = 10, .col = 40, .pre_pre = 0, .act = 0, .read = 1, .write = 0, .pre_post = 1}},
	{1, {.row = 0, .col = 0, .pre_pre = 0, .act = 0, .read = 0, .write = 0, .pre_post = 1}},
	{2, {.row = 10, .col = 0, .pre_pre = 0, .act = 1, .read = 1, .write = 0, .pre_post = 0}},
	{3, {.row = 10, .col = 0, .pre_pre = 0, .act = 1, .read = 1, .write = 0, .pre_post = 0}},
	{2, {.row = 10, .col = 8, .pre_pre = 0, .act = 0, .read = 1, .write = 0, .pre_post = 1}},
	{3, {.row = 10, .col = 8, .pre_pre = 0, .act = 0, .read = 1, .write = 0, .pre_post = 1}},
	{4, {.row = 10, .col = 0, .pre_pre = 0, .act = 1, .read = 1, .write = 0, .pre_post = 0}},
	{5, {.row = 10, .col = 0, .pre_pre = 0, .act = 1, .read = 1, .write = 0, .pre_post = 0}},
	{4, {.row = 10, .col = 8, .pre_pre = 0, .act = 0, .read = 1, .write = 0, .pre_post = 1}},
	{5, {.row = 10, .col = 8, .pre_pre = 0, .act = 0, .read = 1, .write = 0, .pre_post = 1}},
	{0, {.row = 11, .col = 0, .pre_pre = 0, .act = 1, .read = 1, .write = 0, .pre_post = 0}},
	{1, {.row = 11, .col = 0, .pre_pre = 0, .act = 1, .read = 1, .write = 0, .pre_post = 0}},
	{0, {.row = 11, .col = 8, .pre_pre = 0, .act = 0, .read = 1, .write = 0, .pre_post = 1}},
	{1, {.row = 11, .col = 8, .pre_pre = 0, .act = 0, .read = 1, .write = 0, .pre_post = 1}},

};

/** Unit test for mc_control::CmdArb_DDR4 */
template <unsigned int BUS_WIDTH, unsigned int DRAM_BANKS, unsigned int THREADS>
class Test_CmdArb_DDR4 : public SimdTest
{
public:
	/** FIFO of descriptors
	 * @todo Depth? */
	sc_in<bool> in_clk{"in_clk"};

	/** Command FIFOs, one per bank */
	sc_port<tlm_fifo_put_if<cmd_DDR<BUS_WIDTH,THREADS> > > out_cmd_fifo[DRAM_BANKS];

	/** DQ reservation FIFO */
	sc_fifo_in<DQ_reservation<BUS_WIDTH,DRAM_BANKS,THREADS> >
					in_dq_fifo{"in_dq_fifo"};

	/** Command gen is busy */
	sc_inout<bool> out_cmdgen_busy{"out_cmdgen_busy"};

	/** Cycle counter */
	sc_inout<long> out_cycle{"in_cycle"};

	/** All banks precharged */
	sc_in<bool> in_allpre{"in_allpre"};

	/** Workgroup done */
	sc_fifo_in<RequestTarget> in_done_dst{"in_done_dst"};

	/** Construct test thread */
	SC_CTOR(Test_CmdArb_DDR4) : cycle(0)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();

		SC_THREAD(thread_cycle);
		sensitive << in_clk.pos();
	}
private:
	long cycle;

	/** Main thread */
	void
	thread_lt(void)
	{
		unsigned int in, out;
		DQ_reservation<BUS_WIDTH,DRAM_BANKS,THREADS> res;
		unsigned int entries = sizeof(test_ptrn_1)/sizeof(test_ptrn);

		in = 0;
		out = 0;
		while (out < entries) {
			if (in < entries && out_cmd_fifo[test_ptrn_1[in].bank]->nb_can_put()) {
				out_cmd_fifo[test_ptrn_1[in].bank]->put(test_ptrn_1[in].rwp);

				out_cmdgen_busy.write(in != (entries - 1));

				if (!test_ptrn_1[in].rwp.read &&
				    !test_ptrn_1[in].rwp.write)
					out++;

				in++;
			}

			while (in_dq_fifo.num_available()) {
				res = in_dq_fifo.read();
				out++;
				std::cout << res << std::endl;
			}
			wait();
		}

		while (!in_allpre.read())
			wait();

		assert(in_done_dst.num_available() == 1);
		assert(in_done_dst.read() == RequestTarget(0,TARGET_NONE));

		test_finish();
	}

	void
	thread_cycle(void)
	{
		while (true) {
			out_cycle.write(cycle++);
			wait();
		}
	}
};

}

using namespace mc_control;
using namespace mc_test;

int
sc_main(int argc, char* argv[])
{
	sc_fifo<DQ_reservation<16,MC_DRAM_BANKS,1024> > dq_fifo;
	sc_signal<bool> ref_pending;
	sc_signal<bool> cmdgen_busy;
	sc_signal<long> cycle;
	sc_signal<bool> allpre;
	sc_signal<bool> ref;
	sc_fifo<RequestTarget> done_dst(1);
	std::vector<tlm_fifo<cmd_DDR<16,1024> > *> fifo_cmd(MC_DRAM_BANKS);

	sc_set_time_resolution(1, SC_PS);
	sc_clock clk("clk", sc_time(10./16., SC_NS));

	CmdArb_DDR4<16,MC_DRAM_BANKS,1024> my_cmdarb("my_cmdarb");
	my_cmdarb.in_clk(clk);
	my_cmdarb.out_dq_fifo(dq_fifo);
	my_cmdarb.out_ref_pending(ref_pending);
	my_cmdarb.in_cmdgen_busy(cmdgen_busy);
	my_cmdarb.in_cycle(cycle);
	my_cmdarb.out_allpre(allpre);
	my_cmdarb.out_ref(ref);
	my_cmdarb.out_done_dst(done_dst);

	Test_CmdArb_DDR4<16,MC_DRAM_BANKS,1024> my_cmdarb_test("my_cmdarb_test");
	my_cmdarb_test.in_clk(clk);
	my_cmdarb_test.in_dq_fifo(dq_fifo);
	my_cmdarb_test.out_cmdgen_busy(cmdgen_busy);
	my_cmdarb_test.out_cycle(cycle);
	my_cmdarb_test.in_allpre(allpre);
	my_cmdarb_test.in_done_dst(done_dst);

	for (unsigned int i = 0; i < MC_DRAM_BANKS; i++) {
		fifo_cmd[i] = new tlm_fifo<cmd_DDR<16,1024> >(sc_gen_unique_name("fifo_rwp"));
		my_cmdarb.in_cmd_fifo[i](*fifo_cmd[i]);
		my_cmdarb_test.out_cmd_fifo[i](*fifo_cmd[i]);
	}

	sc_core::sc_start(1800, sc_core::SC_NS);

	assert(my_cmdarb_test.has_finished());

	return 0;
}
