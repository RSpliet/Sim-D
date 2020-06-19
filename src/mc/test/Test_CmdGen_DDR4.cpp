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

#include "mc/control/CmdGen_DDR4.h"
#include "util/defaults.h"
#include "util/SimdTest.h"

using namespace sc_core;
using namespace sc_dt;
using namespace mc_model;
using namespace mc_control;
using namespace simd_test;

namespace mc_test {

/** @internal Test pattern for the CmdGen object */
typedef struct {
	/** Tested request */
	burst_request<16,1024> req;

	/** Bank for which resulting commands are expected */
	sc_uint<20> bank;

	/** True iff an activate command is required */
	bool req_act;

	/** The read/write/precharge request we expect */
	cmd_DDR<16,1024> rwp;

	/** True iff our paired bank should receive a precharge as well */
	bool sec_pre;
} test_ptrn;

/** @internal An array of consecutive tests. First item is the requested
 * address, rest of the items describe the expected result.
 */
static test_ptrn test_ptrn_1[]{
	{{0x140004,0xf0f0,false,0,0}, 0, 1, {.row = 20, .col = 0, .act = 1, .read = 1, .write = 0, .pre_post = 0}, false},
	{{0x140040,0x03ff,false,0,0}, 1, 1, {.row = 20, .col = 0, .act = 1, .read = 1, .write = 0, .pre_post = 0}, false},
	{{0x140080,0x03ff,false,0,0}, 0, 0, {.row = 20, .col = 8, .act = 0, .read = 1, .write = 0, .pre_post = 0}, false},
	{{0x1400c0,0x03ff,false,0,0}, 1, 0, {.row = 20, .col = 8, .act = 0, .read = 1, .write = 0, .pre_post = 1}, true},
	{{0x160000,0x03ff,false,0,0}, 0, 1, {.row = 22, .col = 0, .act = 1, .read = 1, .write = 0, .pre_post = 1}, false},
	{{0x170000,0x03ff,false,0,0}, 0, 1, {.row = 23, .col = 0, .act = 1, .read = 1, .write = 0, .pre_post = 1}, false},
	{{0x17c000,0x03ff,false,0,0}, 6, 1, {.row = 23, .col = 0, .act = 1, .read = 1, .write = 0, .pre_post = 1}, false},
};

/** Unit test for mc_control::CmdGen_DDR4 */
template <unsigned int BUS_WIDTH, unsigned int DRAM_BANKS, unsigned int THREADS>
class Test_CmdGen_DDR4 : public SimdTest
{
public:
	/** FIFO of descriptors
	 * @todo Depth? */
	sc_fifo_out<burst_request<BUS_WIDTH,THREADS> > out_req_fifo{"out_req_fifo"};

	/** One FIFO per bank - CAS/Precharge commands
	 * @todo FIFO depth? */
	sc_port<tlm_fifo<cmd_DDR<BUS_WIDTH,THREADS> > > in_fifo_rwp[DRAM_BANKS];

	sc_in<bool> in_busy{"in_busy"};

	/** Construct test thread */
	SC_CTOR(Test_CmdGen_DDR4)
	{
		SC_THREAD(thread_lt);
	}
private:
	/** UNUSED: Print out the contents of all FIFOs */
	void
	print_fifos(void)
	{
		unsigned int i;
		cmd_DDR<BUS_WIDTH,THREADS> rwp;

		for (i = 0; i < DRAM_BANKS; i++) {
			while (in_fifo_rwp[i]->used()) {
				rwp = in_fifo_rwp[i]->get();
				std::cout << "@" << sc_time_stamp() << ": B"
						<< i << ": " << rwp <<  std::endl;
			}
		}
	}

	/** Main thread */
	void
	thread_lt(void)
	{
		cmd_DDR<BUS_WIDTH,THREADS> rwp;
		unsigned int i;

		for (i = 0; i < sizeof(test_ptrn_1)/sizeof(test_ptrn_1[0]); i++) {
			std::cout << "@" << sc_time_stamp() << ": "
					<< test_ptrn_1[i].req << std::endl;
			if (i + 1 < sizeof(test_ptrn_1)/sizeof(test_ptrn_1[0])) {
				test_ptrn_1[i].req.addr_next =
						test_ptrn_1[i+1].req.addr;
				test_ptrn_1[i].req.last = false;
			} else {
				test_ptrn_1[i].req.addr_next = 0xffffffff;
				test_ptrn_1[i].req.last = true;
			}
			out_req_fifo.write(test_ptrn_1[i].req);
			wait(10, SC_NS);

			/* RWP command */
			assert(in_fifo_rwp[test_ptrn_1[i].bank]->used() > 0);
			rwp = in_fifo_rwp[test_ptrn_1[i].bank]->get();
			std::cout << "        " << rwp << std::endl;
			assert(rwp.act == test_ptrn_1[i].rwp.act);
			assert(rwp.row == test_ptrn_1[i].rwp.row);
			assert(rwp.col == test_ptrn_1[i].rwp.col);
			assert(rwp.pre_post == test_ptrn_1[i].rwp.pre_post);
			assert(rwp.read == test_ptrn_1[i].rwp.read);
			assert(rwp.write == test_ptrn_1[i].rwp.write);

			/** Second precharge command */
			assert (test_ptrn_1[i].sec_pre || in_fifo_rwp[test_ptrn_1[i].bank ^ 0x1]->used() == 0);
			if (test_ptrn_1[i].sec_pre) {
				assert(in_fifo_rwp[test_ptrn_1[i].bank ^ 0x1]->used() > 0);
				rwp = in_fifo_rwp[test_ptrn_1[i ^ 0x1].bank]->get();
				std::cout << "        " << rwp << std::endl;
				assert(rwp.pre_post == 1);
				assert(rwp.read == 0);
				assert(rwp.write == 0);
			}

			assert(in_busy.read() == !test_ptrn_1[i].req.last);
			std::cout << "-> Pass" << std::endl;
		}

		test_finish();
	}
};

}

using namespace mc_control;
using namespace mc_test;

int
sc_main(int argc, char* argv[])
{
	sc_fifo<burst_request<16,1024> > req_fifo("req_fifo");
	sc_signal<bool> busy;
	std::vector<tlm_fifo<cmd_DDR<16,1024> > *> fifo_rwp(MC_DRAM_BANKS);

	sc_set_time_resolution(1, SC_PS);
	sc_clock clk("clk", sc_time(10, SC_NS));

	CmdGen_DDR4<16,MC_DRAM_BANKS,MC_DRAM_COLS,MC_DRAM_ROWS,1024> my_cmdgen("my_cmdgen");
	my_cmdgen.in_clk(clk);
	my_cmdgen.in_req_fifo(req_fifo);
	my_cmdgen.out_busy(busy);

	Test_CmdGen_DDR4<16,MC_DRAM_BANKS,1024> my_cmdgen_test("my_cmdgen_test");
	my_cmdgen_test.out_req_fifo(req_fifo);
	my_cmdgen_test.in_busy(busy);

	for (unsigned int i = 0; i < MC_DRAM_BANKS; i++) {
		fifo_rwp[i] = new tlm_fifo<cmd_DDR<16,1024> >(sc_gen_unique_name("fifo_rwp"));
		my_cmdgen.out_fifo[i](*fifo_rwp[i]);

		my_cmdgen_test.in_fifo_rwp[i](*fifo_rwp[i]);
	}

	sc_core::sc_start(4000, sc_core::SC_NS);

	assert(my_cmdgen_test.has_finished());

	return 0;
}
