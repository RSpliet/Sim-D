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

#include "sp/control/StrideSequencer.h"
#include "mc/model/burst_request.h"
#include "util/SimdTest.h"
#include "util/defaults.h"

using namespace sc_core;
using namespace sc_dt;
using namespace mc_model;
using namespace sp_control;
using namespace simd_test;

namespace sp_test {

static DQ_reservation<4,COMPUTE_THREADS> stride_ptrn_1[]{
		{0x0,0xf,false,TARGET_REG,{0,0,1,1},{0,1,0,1}},
		{0x10,0xf,false,TARGET_REG,{2,2,3,3},{0,1,0,1}},
		{0x20,0xf,false,TARGET_REG,{4,4,5,5},{0,1,0,1}},
		{0x30,0x3,false,TARGET_REG,{6,6,0,0},{0,1,0,0}},
		{0x500,0xf,false,TARGET_REG,{64,64,65,65},{0,1,0,1}},
		{0x510,0xf,false,TARGET_REG,{66,66,67,67},{0,1,0,1}},
		{0x520,0xf,false,TARGET_REG,{68,68,69,69},{0,1,0,1}},
		{0x530,0x3,false,TARGET_REG,{70,70,0,0},{0,1,0,0}},
		{0xa00,0xf,false,TARGET_REG,{128,128,129,129},{0,1,0,1}},
		{0xa10,0xf,false,TARGET_REG,{130,130,131,131},{0,1,0,1}},
		{0xa20,0xf,false,TARGET_REG,{132,132,133,133},{0,1,0,1}},
		{0xa30,0x3,false,TARGET_REG,{134,134,0,0},{0,1,0,0}},
		{0xf00,0xf,false,TARGET_REG,{192,192,193,193},{0,1,0,1}},
		{0xf10,0xf,false,TARGET_REG,{194,194,195,195},{0,1,0,1}},
		{0xf20,0xf,false,TARGET_REG,{196,196,197,197},{0,1,0,1}},
		{0xf30,0x3,false,TARGET_REG,{198,198,0,0},{0,1,0,0}},
		{0x1400,0xf,false,TARGET_REG,{256,256,257,257},{0,1,0,1}},
		{0x1410,0xf,false,TARGET_REG,{258,258,259,259},{0,1,0,1}},
		{0x1420,0xf,false,TARGET_REG,{260,260,261,261},{0,1,0,1}},
		{0x1430,0x3,false,TARGET_REG,{262,262,0,0},{0,1,0,0}},
};

static DQ_reservation<4,COMPUTE_THREADS> stride_ptrn_2[]{
		{0x4,0xf,false,TARGET_REG,{0,0,1,1},{0,1,0,1}},
		{0x14,0xf,false,TARGET_REG,{2,2,3,3},{0,1,0,1}},
		{0x24,0xf,false,TARGET_REG,{4,4,5,5},{0,1,0,1}},
		{0x34,0x3,false,TARGET_REG,{6,6,0,0},{0,1,0,0}},
		{0x504,0xf,false,TARGET_REG,{64,64,65,65},{0,1,0,1}},
		{0x514,0xf,false,TARGET_REG,{66,66,67,67},{0,1,0,1}},
		{0x524,0xf,false,TARGET_REG,{68,68,69,69},{0,1,0,1}},
		{0x534,0x3,false,TARGET_REG,{70,70,0,0},{0,1,0,0}},
		{0xa04,0xf,false,TARGET_REG,{128,128,129,129},{0,1,0,1}},
		{0xa14,0xf,false,TARGET_REG,{130,130,131,131},{0,1,0,1}},
		{0xa24,0xf,false,TARGET_REG,{132,132,133,133},{0,1,0,1}},
		{0xa34,0x3,false,TARGET_REG,{134,134,0,0},{0,1,0,0}},
		{0xf04,0xf,false,TARGET_REG,{192,192,193,193},{0,1,0,1}},
		{0xf14,0xf,false,TARGET_REG,{194,194,195,195},{0,1,0,1}},
		{0xf24,0xf,false,TARGET_REG,{196,196,197,197},{0,1,0,1}},
		{0xf34,0x3,false,TARGET_REG,{198,198,0,0},{0,1,0,0}},
		{0x1404,0xf,false,TARGET_REG,{256,256,257,257},{0,1,0,1}},
		{0x1414,0xf,false,TARGET_REG,{258,258,259,259},{0,1,0,1}},
		{0x1424,0xf,false,TARGET_REG,{260,260,261,261},{0,1,0,1}},
		{0x1434,0x3,false,TARGET_REG,{262,262,0,0},{0,1,0,0}},
};

static DQ_reservation<4,COMPUTE_THREADS> stride_ptrn_3[]{
		{0x4,0xf,false,TARGET_CAM,{512,513,514,515}},
		{0x14,0xf,false,TARGET_CAM,{516,517,518,519}},
		{0x24,0xf,false,TARGET_CAM,{520,521,522,523}},
		{0x34,0xf,false,TARGET_CAM,{524,525,526,527}},
		{0x44,0xf,false,TARGET_CAM,{528,529,530,531}},
		{0x54,0xf,false,TARGET_CAM,{532,533,534,535}},
		{0x64,0xf,false,TARGET_CAM,{536,537,538,539}},
		{0x74,0xf,false,TARGET_CAM,{540,541,542,543}},
		{0x84,0xf,false,TARGET_CAM,{544,545,546,547}},
		{0x94,0xf,false,TARGET_CAM,{548,549,550,551}},
		{0xa4,0xf,false,TARGET_CAM,{552,553,554,555}},
		{0xb4,0xf,false,TARGET_CAM,{556,557,558,559}},
		{0xc4,0xf,false,TARGET_CAM,{560,561,562,563}},
		{0xd4,0xf,false,TARGET_CAM,{564,565,566,567}},
		{0xe4,0xf,false,TARGET_CAM,{568,569,570,571}},
		{0xf4,0x3,false,TARGET_CAM,{572,573,0,0}},
		{0x3f4,0x8,false,TARGET_CAM,{0,0,0,767}},
		{0x404,0xf,false,TARGET_CAM,{768,769,770,771}},
		{0x414,0xf,false,TARGET_CAM,{772,773,774,775}},
		{0x424,0xf,false,TARGET_CAM,{776,777,778,779}},
		{0x434,0xf,false,TARGET_CAM,{780,781,782,783}},
		{0x444,0xf,false,TARGET_CAM,{784,785,786,787}},
		{0x454,0xf,false,TARGET_CAM,{788,789,790,791}},
		{0x464,0xf,false,TARGET_CAM,{792,793,794,795}},
		{0x474,0xf,false,TARGET_CAM,{796,797,798,799}},
		{0x484,0xf,false,TARGET_CAM,{800,801,802,803}},
		{0x494,0xf,false,TARGET_CAM,{804,805,806,807}},
		{0x4a4,0xf,false,TARGET_CAM,{808,809,810,811}},
		{0x4b4,0xf,false,TARGET_CAM,{812,813,814,815}},
		{0x4c4,0xf,false,TARGET_CAM,{816,817,818,819}},
		{0x4d4,0xf,false,TARGET_CAM,{820,821,822,823}},
		{0x4e4,0xf,false,TARGET_CAM,{824,825,826,827}},
		{0x4f4,0x1,false,TARGET_CAM,{828,0,0,0}},
};

/** Unit test for sp_control::StrideSequencer */
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

	/** Ready to accept next descriptor */
	sc_fifo_in<sc_uint<1> > in_wg_done{"in_wg_done"};

	/** DQ finished the last transfer. */
	sc_inout<bool> out_dq_done{"out_dq_done"};

	/** Generated request */
	sc_fifo_in<DQ_reservation<BUS_WIDTH,THREADS> > in_dq_fifo{"out_dq_fifo"};

	/** Register file register targeted. */
	sc_in<AbstractRegister> in_rf_reg_w{"rf_reg_w"};

	/** True iff this stride pattern issues a write. */
	sc_in<bool> in_write{"in_write"};

	/** Scheduling options. */
	sc_inout<sc_bv<WSS_SENTINEL> > out_sched_opts{"out_sched_opts"};

	/** Ticket number that's ready to pop. */
	sc_inout<sc_uint<4> > out_ticket_pop{"out_ticket_pop"};

	/** Construct test thread */
	SC_CTOR(Test_StrideSequencer)
	{
		SC_THREAD(thread);
		sensitive << in_clk.pos();
	}
private:
	void
	test_do(stride_descriptor &desc,
		DQ_reservation<BUS_WIDTH,THREADS> *reqs, unsigned int elems)
	{
		unsigned int i;
		DQ_reservation<BUS_WIDTH,THREADS> req;
		bool fail = false;

		reqs[elems-1].last = true;

		out_desc_fifo.write(desc);
		out_trigger.write(true);
		wait();
		wait();

		if (desc.getTargetType() == TARGET_REG)
			assert(*(desc.getTargetReg()) == in_rf_reg_w.read());

		i = 0;

		do {
			while (in_dq_fifo.num_available()) {
				assert(i < elems);
				in_dq_fifo.read(req);

				if(!(reqs[i] == req))
					fail = true;
				std::cout << req << std::endl;
				i++;
			}

			wait();
		} while (!req.last);

		out_dq_done.write(true);
		assert(!fail);
		assert(i == elems);
		assert(!in_dq_fifo.num_available());
		wait();
		out_dq_done.write(false);
		assert(in_wg_done.read() == 0);
	}


	/** Main thread.
	 * @todo Test DQ_allpre */
	void
	thread(void)
	{
		stride_descriptor desc;
		AbstractRegister *reg;
		unsigned int elems;

		out_ticket_pop.write(0);
		out_sched_opts.write(0);

		/* Test one: to register.
		 * @todo This tests "2-vector" loads, which are no longer
		 * part of the ISA. When support is properly scrapped, transform
		 * this test into regular vector loads. */
		elems = sizeof(stride_ptrn_1)/sizeof(stride_ptrn_1[0]);
		reg = new AbstractRegister(0, REGISTER_VGPR, 5);
		desc = stride_descriptor(*reg);
		desc.dst_period = 64;
		desc.period = 320;
		desc.period_count = 5;
		desc.words = 14;
		desc.addr = 0x0;
		desc.idx_transform = IDX_TRANSFORM_VEC2;
		desc.write = false;
		test_do(desc, stride_ptrn_1, elems);

		/* Test two: to register. Same test, different addresses. */
		elems = sizeof(stride_ptrn_2)/sizeof(stride_ptrn_2[0]);
		reg = new AbstractRegister(0, REGISTER_VGPR, 5);
		desc = stride_descriptor(*reg);
		desc.dst_period = 64;
		desc.period = 320;
		desc.period_count = 5;
		desc.words = 14;
		desc.addr = 0x4;
		desc.dst_offset = 0;
		desc.idx_transform = IDX_TRANSFORM_VEC2;
		desc.write = false;
		test_do(desc, stride_ptrn_2, elems);

		/* Test two: to CAM register. */
		elems = sizeof(stride_ptrn_3)/sizeof(stride_ptrn_3[0]);
		reg = new AbstractRegister(0, REGISTER_VSP, VSP_MEM_DATA);
		desc = stride_descriptor(*reg);
		desc.addr = 0x4;
		desc.dst_period = 128;
		desc.period = 255;
		desc.period_count = 2;
		desc.words = 62;
		desc.dst_offset = 512;
		desc.idx_transform = IDX_TRANSFORM_UNIT;
		test_do(desc, stride_ptrn_3, elems);

		test_finish();
	}
};

}

using namespace sp_control;
using namespace sp_test;

int
sc_main(int argc, char* argv[])
{
	sc_fifo<bool> trigger(2);
	sc_signal<sc_uint<32> > addr;
	sc_signal<sc_bv<4> > wordmask;
	sc_fifo<sc_uint<1> > wg_done;
	sc_signal<bool> dq_done;
	sc_signal<bool> write;
	sc_signal<AbstractRegister> rf_reg_w;
	sc_signal<long> cycle;
	sc_signal<sc_bv<WSS_SENTINEL> > sched_opts;
	sc_signal<sc_uint<4> > ticket_pop;

	sc_fifo<stride_descriptor> desc_fifo("desc_fifo");
	sc_fifo<DQ_reservation<4,COMPUTE_THREADS> > dq_fifo("req_fifo");

	sc_clock clk("clk", sc_time(10./16., SC_NS));

	StrideSequencer<0,4,COMPUTE_THREADS> my_sseq("my_sseq");
	my_sseq.in_clk(clk);
	my_sseq.in_desc_fifo(desc_fifo);
	my_sseq.in_trigger(trigger);
	my_sseq.out_wg_done(wg_done);
	my_sseq.in_dq_done(dq_done);
	my_sseq.out_dq_fifo(dq_fifo);
	my_sseq.out_rf_reg_w(rf_reg_w);
	my_sseq.out_write(write);
	my_sseq.in_sched_opts(sched_opts);
	my_sseq.in_ticket_pop(ticket_pop);

	Test_StrideSequencer<4,COMPUTE_THREADS> my_sseq_test("my_sseq_test");
	my_sseq_test.in_clk(clk);
	my_sseq_test.out_desc_fifo(desc_fifo);
	my_sseq_test.in_dq_fifo(dq_fifo);
	my_sseq_test.out_trigger(trigger);
	my_sseq_test.in_wg_done(wg_done);
	my_sseq_test.out_dq_done(dq_done);
	my_sseq_test.in_rf_reg_w(rf_reg_w);
	my_sseq_test.in_write(write);
	my_sseq_test.out_sched_opts(sched_opts);
	my_sseq_test.out_ticket_pop(ticket_pop);

	sc_core::sc_start(700, SC_NS);

	assert(my_sseq_test.has_finished());

	return 0;
}
