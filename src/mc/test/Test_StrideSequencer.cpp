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
#include "util/defaults.h"

using namespace sc_core;
using namespace sc_dt;
using namespace mc_model;
using namespace mc_control;
using namespace simd_test;

namespace mc_test {

static burst_request<16,COMPUTE_THREADS> stride_ptrn_1[]{
		{0x140000,0xfffe,false,0,0x0},
		{0x140040,0x000f,false,0,0x3c},
		{0x1400c0,0xc000,false,0,0x4c},
		{0x140100,0xffff,false,0,0x54},
		{0x140140,0x0001,false,0,0x94},
		{0x1401c0,0xf800,false,0,0x98},
		{0x140200,0x3fff,false,0,0xac},
		{0x1402c0,0xff00,false,0,0xe4},
		{0x140300,0x07ff,false,0,0x104},
		{0x1403c0,0xffe0,false,0,0x130},
		{0x140400,0x00ff,false,0,0x15c},
		{0x1404c0,0xfffc,false,0,0x17c},
		{0x140500,0x001f,false,0,0x1b4},
		{0x140580,0x8000,false,0,0x1c8},
		{0x1405c0,0xffff,false,0,0x1cc},
		{0x140600,0x0003,false,0,0x20c},
		{0x140680,0xf000,false,0,0x214},
		{0x1406c0,0x7fff,false,0,0x224},
		{0x140780,0xfe00,false,0,0x260},
		{0x1407c0,0x0fff,false,0,0x27c},
		{0x140880,0xffc0,false,0,0x2ac},
		{0x1408c0,0x01ff,false,0,0x2d4},
		{0x140980,0xfff8,false,0,0x2f8},
		{0x1409c0,0x003f,false,0,0x32c},
		{0x140a80,0xffff,false,0,0x344},
		{0x140ac0,0x0007,false,0,0x384},
		{0x140b40,0xe000,false,0,0x390},
		{0x140b80,0xffff,false,0,0x39c},
		{0x140c40,0xfc00,false,0,0x3dc},
		{0x140c80,0x1fff,false,0,0x3f4},
		{0x140d40,0xff80,false,0,0x428},
		{0x140d80,0x03ff,false,0,0x44c},
		{0x140e40,0xfff0,false,0,0x474},
		{0x140e80,0x007f,false,0,0x4a4}
};

static burst_request<16,COMPUTE_THREADS> stride_ptrn_2[]{
		{0x0,0x3fff,false,0,TARGET_REG,{0,0,1,1,2,2,3,3,4,4,5,5,6,6,0,0},{0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0}},
		{0x500,0x3fff,false,0,TARGET_REG,{64,64,65,65,66,66,67,67,68,68,69,69,70,70,0,0},{0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0}},
		{0xa00,0x3fff,false,0,TARGET_REG,{128,128,129,129,130,130,131,131,132,132,133,133,134,134,0,0},{0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0}},
		{0xf00,0x3fff,false,0,TARGET_REG,{192,192,193,193,194,194,195,195,196,196,197,197,198,198,0,0},{0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0}},
		{0x1400,0x3fff,false,0,TARGET_REG,{256,256,257,257,258,258,259,259,260,260,261,261,262,262,0,0},{0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0}},
		{0x1900,0x3fff,false,0,TARGET_REG,{320,320,321,321,322,322,323,323,324,324,325,325,326,326,0,0},{0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0}},
		{0x1e00,0x3fff,false,0,TARGET_REG,{384,384,385,385,386,386,387,387,388,388,389,389,390,390,0,0},{0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0}},
		{0x2300,0x3fff,false,0,TARGET_REG,{448,448,449,449,450,450,451,451,452,452,453,453,454,454,0,0},{0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0}},
		{0x2800,0x3fff,false,0,TARGET_REG,{512,512,513,513,514,514,515,515,516,516,517,517,518,518,0,0},{0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0}},
		{0x2d00,0x3fff,false,0,TARGET_REG,{576,576,577,577,578,578,579,579,580,580,581,581,582,582,0,0},{0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0}},
		{0x3200,0x3fff,false,0,TARGET_REG,{640,640,641,641,642,642,643,643,644,644,645,645,646,646,0,0},{0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0}},
		{0x3700,0x3fff,false,0,TARGET_REG,{704,704,705,705,706,706,707,707,708,708,709,709,710,710,0,0},{0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0}},
		{0x3c00,0x3fff,false,0,TARGET_REG,{768,768,769,769,770,770,771,771,772,772,773,773,774,774,0,0},{0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0}},
		{0x4100,0x3fff,false,0,TARGET_REG,{832,832,833,833,834,834,835,835,836,836,837,837,838,838,0,0},{0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0}},
		{0x4600,0x3fff,false,0,TARGET_REG,{896,896,897,897,898,898,899,899,900,900,901,901,902,902,0,0},{0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0}},
		{0x4b00,0x3fff,false,0,TARGET_REG,{960,960,961,961,962,962,963,963,964,964,965,965,966,966,0,0},{0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0}}
};

static burst_request<16,COMPUTE_THREADS> stride_ptrn_3[]{
		{0x0,0xfffe,false,0,TARGET_CAM,{0,512,513,514,515,516,517,518,519,520,521,522,523,524,525,526}},
		{0x40,0xffff,false,0,TARGET_CAM,{527,528,529,530,531,532,533,534,535,536,537,538,539,540,541,542}},
		{0x80,0xffff,false,0,TARGET_CAM,{543,544,545,546,547,548,549,550,551,552,553,554,555,556,557,558}},
		{0xc0,0x7fff,false,0,TARGET_CAM,{559,560,561,562,563,564,565,566,567,568,569,570,571,572,573,0}},
		{0x400,0xfffe,false,0,TARGET_CAM,{0,768,769,770,771,772,773,774,775,776,777,778,779,780,781,782}},
		{0x440,0xffff,false,0,TARGET_CAM,{783,784,785,786,787,788,789,790,791,792,793,794,795,796,797,798}},
		{0x480,0xffff,false,0,TARGET_CAM,{799,800,801,802,803,804,805,806,807,808,809,810,811,812,813,814}},
		{0x4c0,0x7fff,false,0,TARGET_CAM,{815,816,817,818,819,820,821,822,823,824,825,826,827,828,829,0}},
		{0x800,0xfffe,false,0,TARGET_CAM,{0,1024,1025,1026,1027,1028,1029,1030,1031,1032,1033,1034,1035,1036,1037,1038}},
		{0x840,0xffff,false,0,TARGET_CAM,{1039,1040,1041,1042,1043,1044,1045,1046,1047,1048,1049,1050,1051,1052,1053,1054}},
		{0x880,0xffff,false,0,TARGET_CAM,{1055,1056,1057,1058,1059,1060,1061,1062,1063,1064,1065,1066,1067,1068,1069,1070}},
		{0x8c0,0x7fff,false,0,TARGET_CAM,{1071,1072,1073,1074,1075,1076,1077,1078,1079,1080,1081,1082,1083,1084,1085,0}},
		{0xc00,0xfffe,false,0,TARGET_CAM,{0,1280,1281,1282,1283,1284,1285,1286,1287,1288,1289,1290,1291,1292,1293,1294}},
		{0xc40,0xffff,false,0,TARGET_CAM,{1295,1296,1297,1298,1299,1300,1301,1302,1303,1304,1305,1306,1307,1308,1309,1310}},
		{0xc80,0xffff,false,0,TARGET_CAM,{1311,1312,1313,1314,1315,1316,1317,1318,1319,1320,1321,1322,1323,1324,1325,1326}},
		{0xcc0,0x7fff,false,0,TARGET_CAM,{1327,1328,1329,1330,1331,1332,1333,1334,1335,1336,1337,1338,1339,1340,1341,0}}
};

/** Unit test for mc_control::StrideSequencer */
template <unsigned int BUS_WIDTH, unsigned int THREADS>
class Test_StrideSequencer : public SimdTest
{
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
	SC_CTOR(Test_StrideSequencer)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();

		SC_THREAD(thread_cycle);
		sensitive << in_clk.pos();
	}
private:
	void
	golden_list_postprocess(burst_request<BUS_WIDTH,THREADS> *l, unsigned int elems)
	{
		unsigned int i;

		for (i = 0; i < elems - 1; i++)
			l[i].addr_next = l[i+1].addr;

		l[elems-1].addr_next = 0xffffffff;
		l[elems-1].last = true;
	}

	void
	test_do(stride_descriptor &desc, burst_request<BUS_WIDTH,THREADS> *reqs,
			unsigned int elems)
	{
		unsigned int i;
		burst_request<BUS_WIDTH,THREADS> req;
		bool fail = false;

		golden_list_postprocess(reqs, elems);

		out_desc_fifo.write(desc);
		out_ref_pending.write(true);
		out_trigger.write(true);
		wait();
		wait();
		wait();
		wait();
		wait();
		wait();
		assert(in_req_fifo.num_available() == 0);
		out_ref_pending.write(false);
		wait();
		wait();

		if (desc.getTargetType() == TARGET_REG)
			assert(*(desc.getTargetReg()) == in_dst_reg.read());

		i = 0;

		do {
			while (in_req_fifo.num_available()) {
				assert(i < elems);
				in_req_fifo.read(req);

				if (!(reqs[i] == req))
					fail = true;

				std::cout << req << std::endl;
				i++;
			}

			wait();
		} while (!req.last);

		assert(!fail);

		wait();
		wait();
		wait();
		out_DQ_allpre.write(true);
		wait();
		out_DQ_allpre.write(false);
		wait();

		assert(i == elems);
		assert(!in_req_fifo.num_available());
		wait();
		assert(in_done.read());
	}


	/** Main thread.
	 * @todo Test DQ_allpre */
	void
	thread_lt(void)
	{
		stride_descriptor desc;
		AbstractRegister *reg;
		unsigned int elems;
		sc_bv<WSS_SENTINEL> sched_opts;

		sched_opts = 0;

		out_sched_opts.write(sched_opts);
		out_ticket_pop.write(0);

		/* Test one: to scratchpad. */
		elems = sizeof(stride_ptrn_1)/sizeof(stride_ptrn_1[0]);

		desc.addr = 0x140004;
		desc.period = 61;
		desc.period_count = 16;
		desc.words = 19;
		desc.dst_period = 19;
		test_do(desc, stride_ptrn_1, elems);

		/* Test two: to register. */
		elems = sizeof(stride_ptrn_2)/sizeof(stride_ptrn_2[0]);
		reg = new AbstractRegister(0, REGISTER_VGPR, 5);
		desc = stride_descriptor(*reg);
		desc.dst_period = 64;
		desc.period = 320;
		desc.period_count = 16;
		desc.words = 14;
		desc.idx_transform = IDX_TRANSFORM_VEC2;
		test_do(desc, stride_ptrn_2, elems);

		/* Test two: to CAM register. */
		elems = sizeof(stride_ptrn_3)/sizeof(stride_ptrn_3[0]);
		reg = new AbstractRegister(0, REGISTER_VSP, VSP_MEM_DATA);
		desc = stride_descriptor(*reg);
		desc.addr = 0x4;
		desc.dst_period = 128;
		desc.period = 256;
		desc.period_count = 4;
		desc.words = 62;
		desc.dst_offset = 512;
		desc.idx_transform = IDX_TRANSFORM_UNIT;
		test_do(desc, stride_ptrn_3, elems);

		test_finish();
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
	sc_fifo<bool> trigger(2);
	sc_signal<sc_uint<32> > addr;
	sc_signal<bool> ref_pending;
	sc_signal<bool> done;
	sc_signal<bool> write;
	sc_signal<bool> dq_allpre;
	sc_signal<RequestTarget> dst;
	sc_signal<AbstractRegister> dst_reg;
	sc_signal<bool> idx_push_trigger;
	sc_fifo<idx_t<1024> > idx;
	sc_signal<long> cycle;
	sc_signal<sc_bv<WSS_SENTINEL> > sched_opts;
	sc_signal<sc_uint<4> > ticket_pop;

	sc_fifo<stride_descriptor> desc_fifo("desc_fifo");
	sc_fifo<burst_request<16,COMPUTE_THREADS> > req_fifo("req_fifo");

	sc_clock clk("clk", sc_time(10./12., SC_NS));

	StrideSequencer<16,COMPUTE_THREADS> my_sseq("my_sseq");
	my_sseq.in_clk(clk);
	my_sseq.in_desc_fifo(desc_fifo);
	my_sseq.in_trigger(trigger);
	my_sseq.in_ref_pending(ref_pending);
	my_sseq.out_req_fifo(req_fifo);
	my_sseq.out_done(done);
	my_sseq.in_DQ_allpre(dq_allpre);
	my_sseq.out_dst(dst);
	my_sseq.out_dst_reg(dst_reg);
	my_sseq.out_idx_push_trigger(idx_push_trigger);
	my_sseq.in_idx(idx);
	my_sseq.in_cycle(cycle);
	my_sseq.in_sched_opts(sched_opts);
	my_sseq.in_ticket_pop(ticket_pop);

	Test_StrideSequencer<16,COMPUTE_THREADS> my_sseq_test("my_sseq_test");
	my_sseq_test.in_clk(clk);
	my_sseq_test.in_req_fifo(req_fifo);
	my_sseq_test.out_trigger(trigger);
	my_sseq_test.out_ref_pending(ref_pending);
	my_sseq_test.out_desc_fifo(desc_fifo);
	my_sseq_test.in_done(done);
	my_sseq_test.out_DQ_allpre(dq_allpre);
	my_sseq_test.in_dst(dst);
	my_sseq_test.in_dst_reg(dst_reg);
	my_sseq_test.in_idx_push_trigger(idx_push_trigger);
	my_sseq_test.out_idx(idx);
	my_sseq_test.out_cycle(cycle);
	my_sseq_test.out_sched_opts(sched_opts);
	my_sseq_test.out_ticket_pop(ticket_pop);

	sc_core::sc_start(700, SC_NS);

	assert(my_sseq_test.has_finished());

	return 0;
}
