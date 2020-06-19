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

#include "compute/control/SimdCluster.h"
#include "model/Buffer.h"
#include "model/stride_descriptor.h"
#include "util/SimdTest.h"

using namespace sc_dt;
using namespace sc_core;
using namespace compute_control;
using namespace simd_test;
using namespace simd_model;

namespace compute_test {

static Instruction op_ptrn[]{
		Instruction(OP_TEST,{.test = TEST_NZ},Operand(REGISTER_PR,0),Operand(REGISTER_VGPR,3)),
		Instruction(NOP,{}),
		Instruction(OP_CPUSH,{.cpush = CPUSH_BRK},{},5),
		Instruction(OP_J,{},{},Operand(8)),
		Instruction(OP_BRK,{},{},Operand(REGISTER_PR,0)),
		Instruction(OP_J,{},{},Operand(4)),
		Instruction(OP_CPOP,{}),
};

template <unsigned int THREADS, unsigned int FPUS,
	unsigned int RCPUS = 32, unsigned int PC_WIDTH = 11,
	unsigned int XLAT_ENTRIES = 32, unsigned int BUS_WIDTH = 16>
class Test_SimdCluster : public SimdTest
{
public:
	sc_in<bool> in_clk{"in_clk"};

	/** Workgroup fifo, incoming from the workscheduler */
	sc_fifo_out<workgroup<THREADS,FPUS> > out_wg{"out_wg"};

	/** Dimensions for currently active program */
	sc_inout<sc_uint<32> > out_work_dim[2];

	/** Width of workgroup of currently active program */
	sc_inout<workgroup_width> out_wg_width{"out_wg_width"};

	/** Scheduling options. */
	sc_inout<sc_bv<WSS_SENTINEL> > out_sched_opts{"out_sched_opts"};

	/** Ticket number that's ready to pop. */
	sc_in<sc_uint<4> > in_ticket_pop{"in_ticket_pop"};

	/****************** Direct pass-through to IMem ******************/
	/** Program upload interface from workscheduler, operand. */
	sc_inout<Instruction> out_prog_op_w[4];
	/** Progrma upload interface from workscheduler, PC. */
	sc_inout<sc_uint<PC_WIDTH> > out_prog_pc_w{"out_prog_pc_w"};
	/** Program upload interface from workscheduler, enable */
	sc_inout<bool> out_prog_w{"out_prog_w"};

	/** Last wg of program has been offered on FIFO */
	sc_inout<bool> out_end_prg{"out_end_prg"};

	/** Execution of final WG finished */
	sc_in<bool> in_exec_fini{"in_exec_fini"};

	/************ Direct pass-through to BufferToPhysXlat ************/
	/** Write a translation table entry */
	sc_inout<bool> out_xlat_w{"out_xlat_w"};

	/** Buffer index to write to. */
	sc_inout<sc_uint<const_log2(XLAT_ENTRIES)> >
					out_xlat_idx_w{"out_xlat_idx_w"};

	/** Physical address indexed by buffer index. */
	sc_inout<Buffer> out_xlat_phys_w{"out_xlat_phys_w"};

	/******** Direct pass-through to Scratchpad BufferToPhysXlat ********/
	/** Write a translation table entry */
	sc_inout<bool> out_sp_xlat_w{"out_sp_xlat_w"};

	/** Buffer index to write to. */
	sc_inout<sc_uint<const_log2(XLAT_ENTRIES)> >
					out_sp_xlat_idx_w{"out_sp_xlat_idx_w"};

	/** Physical address indexed by buffer index. */
	sc_inout<Buffer> out_sp_xlat_phys_w{"out_sp_xlat_phys_w"};

	/*************** Pass-through to the memory controller ************/
	/** DRAM read/write enable bit */
	sc_inout<bool> out_dram_enable{"out_dram_enable"};

	/** DRAM write enable bit */
	sc_inout<bool> out_dram_write{"out_dram_write"};

	/** Destination (Register file/SP, WG) for active DRAM request. */
	sc_inout<RequestTarget> out_dram_dst{"out_dram_dst"};

	/* DRAM requests */
	sc_fifo_in<stride_descriptor> in_desc_fifo{"out_desc_fifo"};

	/** Kick off DRAM request */
	sc_fifo_in<bool> in_dram_kick{"in_dram_kick"};

	/** MC signals whether it's done processing a DRAM request for given WG.
	 *
	 * Using a FIFO to easy crossing clock domains in SystemC. */
	sc_fifo_out<RequestTarget> out_dram_done_dst{"out_dram_done_dst"};

	/** DRAM write mask */
	sc_inout<sc_bv<BUS_WIDTH/4> > out_dram_mask{"out_dram_mask"};

	/** Data to write back to register */
	sc_inout<sc_uint<32> > out_dram_data[4];

	/** Data to write back to register */
	sc_in<sc_uint<32> > in_dram_data[IF_SENTINEL][4];

	/** Refresh in progress. */
	sc_inout<bool> out_dram_ref{"out_dram_ref"};

	/************ Write path to Register file ****************/
	/** Index within register to read/write to */
	sc_inout<reg_offset_t<THREADS> > out_dram_idx[4];

	/** Register addressed by DRAM */
	sc_inout<AbstractRegister> out_dram_reg{"out_dram_reg"};

	/** Write mask taking into account individual lane status */
	sc_in<sc_bv<BUS_WIDTH/4> >
			in_dram_mask{"in_dram_mask"};


	sc_inout<bool> out_dram_idx_push_trigger{"out_dram_idx_push_trigger"};

	sc_fifo_in<idx_t<THREADS> > in_dram_idx{"in_dram_idx"};

	/************ Write path to scratchpads *****************/
	sc_inout<sc_uint<18> > out_dram_sp_addr{"out_dram_sp_addr"};


	SC_CTOR(Test_SimdCluster)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();
	}

private:
	void
	set_work_params(workgroup_width w, unsigned int x, unsigned int y)
	{
		out_wg_width.write(w);
		out_work_dim[0].write(x);
		out_work_dim[1].write(y);
	}

	void
	upload_program(void)
	{
		unsigned int i;
		const unsigned int entries = sizeof(op_ptrn)/sizeof(op_ptrn[0]);

		out_prog_w.write(true);
		for (i = 0; i < entries; i+=2) {
			out_prog_pc_w.write(i);
			out_prog_op_w[0].write(op_ptrn[i]);
			if (i + 1 < entries)
				out_prog_op_w[1].write(op_ptrn[i+1]);
			else
				out_prog_op_w[1].write(Instruction());
			wait();
		}
		out_prog_w.write(false);
	}

	void
	thread_lt(void)
	{
		out_dram_ref.write(false);
		out_sched_opts.write(0);

		/** Really this test just checks sensible SimdCluster wiring. */
		set_work_params(WG_WIDTH_1024, 1920, 1080);
		upload_program();

		out_wg.write(workgroup<THREADS,FPUS>{0, 0, 7});
		out_wg.write(workgroup<THREADS,FPUS>{0, 1024, 7});
		while (true)
			wait();
	}
};

}

using namespace compute_test;

int
main(int argc, char **argv)
{
	unsigned int i,j;

	SimdCluster<COMPUTE_THREADS,COMPUTE_FPUS,COMPUTE_RCPUS,COMPUTE_PC_WIDTH,MC_BIND_BUFS,MC_BUS_WIDTH,SP_BUS_WIDTH> my_sc("my_sc");
	Test_SimdCluster<COMPUTE_THREADS,COMPUTE_FPUS,COMPUTE_RCPUS,COMPUTE_PC_WIDTH,MC_BIND_BUFS,MC_BUS_WIDTH> my_sc_test("my_sc_test");

	sc_clock clk("clk", sc_time(10./12., SC_NS));
	sc_clock clk_dram("clk_dram", sc_time(10./16., SC_NS));

	sc_signal<bool> rst;
	sc_fifo<workgroup<COMPUTE_THREADS,COMPUTE_FPUS> > wg(1);
	sc_signal<sc_uint<32> > work_dim[2];
	sc_signal<workgroup_width> wg_width;
	sc_signal<sc_bv<WSS_SENTINEL> > sched_opts;
	sc_signal<sc_uint<4> > ticket_pop;
	sc_signal<Instruction> prog_op_w[4];
	sc_signal<sc_uint<11> > prog_pc_w;
	sc_signal<bool> prog_w;
	sc_signal<bool> end_prg;
	sc_signal<bool> exec_fini;
	sc_signal<bool> xlat_w;
	sc_signal<sc_uint<const_log2(MC_BIND_BUFS)> > xlat_idx_w;
	sc_signal<Buffer> xlat_phys_w;
	sc_signal<bool> sp_xlat_w;
	sc_signal<sc_uint<const_log2(MC_BIND_BUFS)> > sp_xlat_idx_w;
	sc_signal<Buffer> sp_xlat_phys_w;

	sc_fifo<stride_descriptor> desc_fifo(2);
	sc_fifo<bool> dram_kick(2);
	sc_fifo<RequestTarget> dram_done_dst(1);

	sc_signal<reg_offset_t<COMPUTE_THREADS> > dram_vreg_idx_w[MC_BUS_WIDTH/4];
	sc_signal<sc_uint<32> > dram_data[MC_BUS_WIDTH/4];
	sc_signal<sc_uint<32> > dram_data_r[IF_SENTINEL][MC_BUS_WIDTH/4];
	sc_signal<bool> dram_ref;
	sc_signal<AbstractRegister> dram_reg;
	sc_signal<bool> dram_enable;
	sc_signal<RequestTarget> dram_dst;
	sc_signal<sc_bv<4> > dram_mask;
	sc_signal<sc_bv<4> > o_dram_mask;
	sc_signal<bool> dram_idx_push_trigger;
	sc_fifo<idx_t<COMPUTE_THREADS> > o_dram_idx;
	sc_signal<bool> dram_write;
	sc_signal<sc_uint<18> > dram_sp_addr;

	my_sc.in_clk(clk);
	my_sc.in_clk_dram(clk_dram);
	my_sc.in_rst(rst);
	my_sc.in_wg(wg);
	my_sc.in_work_dim[0](work_dim[0]);
	my_sc.in_work_dim[1](work_dim[1]);
	my_sc.in_wg_width(wg_width);
	my_sc.in_sched_opts(sched_opts);
	my_sc.out_ticket_pop(ticket_pop);
	my_sc.in_prog_pc_w(prog_pc_w);
	my_sc.in_prog_w(prog_w);
	my_sc.in_end_prg(end_prg);
	my_sc.out_exec_fini(exec_fini);
	my_sc.in_xlat_w(xlat_w);
	my_sc.in_xlat_idx_w(xlat_idx_w);
	my_sc.in_xlat_phys_w(xlat_phys_w);
	my_sc.in_sp_xlat_w(sp_xlat_w);
	my_sc.in_sp_xlat_idx_w(sp_xlat_idx_w);
	my_sc.in_sp_xlat_phys_w(sp_xlat_phys_w);
	my_sc.in_dram_enable(dram_enable);
	my_sc.in_dram_write(dram_write);
	my_sc.in_dram_dst(dram_dst);
	my_sc.out_desc_fifo(desc_fifo);
	my_sc.out_dram_kick(dram_kick);
	my_sc.in_dram_done_dst(dram_done_dst);
	my_sc.in_dram_mask(dram_mask);
	my_sc.in_dram_ref(dram_ref);
	my_sc.in_dram_reg(dram_reg);
	my_sc.out_dram_mask(o_dram_mask);
	my_sc.in_dram_idx_push_trigger(dram_idx_push_trigger);
	my_sc.out_dram_idx(o_dram_idx);
	my_sc.in_dram_sp_addr(dram_sp_addr);

	my_sc_test.in_clk(clk);
	my_sc_test.out_wg(wg);
	my_sc_test.out_work_dim[0](work_dim[0]);
	my_sc_test.out_work_dim[1](work_dim[1]);
	my_sc_test.out_wg_width(wg_width);
	my_sc_test.out_sched_opts(sched_opts);
	my_sc_test.in_ticket_pop(ticket_pop);
	my_sc_test.out_prog_pc_w(prog_pc_w);
	my_sc_test.out_prog_w(prog_w);
	my_sc_test.out_end_prg(end_prg);
	my_sc_test.in_exec_fini(exec_fini);
	my_sc_test.out_xlat_w(xlat_w);
	my_sc_test.out_xlat_idx_w(xlat_idx_w);
	my_sc_test.out_xlat_phys_w(xlat_phys_w);
	my_sc_test.out_sp_xlat_w(sp_xlat_w);
	my_sc_test.out_sp_xlat_idx_w(sp_xlat_idx_w);
	my_sc_test.out_sp_xlat_phys_w(sp_xlat_phys_w);
	my_sc_test.out_dram_enable(dram_enable);
	my_sc_test.out_dram_write(dram_write);
	my_sc_test.out_dram_dst(dram_dst);
	my_sc_test.in_desc_fifo(desc_fifo);
	my_sc_test.in_dram_kick(dram_kick);
	my_sc_test.out_dram_done_dst(dram_done_dst);
	my_sc_test.out_dram_mask(dram_mask);
	my_sc_test.out_dram_ref(dram_ref);
	my_sc_test.out_dram_reg(dram_reg);
	my_sc_test.in_dram_mask(o_dram_mask);
	my_sc_test.out_dram_idx_push_trigger(dram_idx_push_trigger);
	my_sc_test.in_dram_idx(o_dram_idx);
	my_sc_test.out_dram_sp_addr(dram_sp_addr);

	for (i = 0; i < 4; i++) {
		my_sc.in_prog_op_w[i](prog_op_w[i]);
		my_sc_test.out_prog_op_w[i](prog_op_w[i]);
		my_sc.in_dram_idx[i](dram_vreg_idx_w[i]);
		my_sc.in_dram_data[i](dram_data[i]);

		my_sc_test.out_dram_idx[i](dram_vreg_idx_w[i]);
		my_sc_test.out_dram_data[i](dram_data[i]);

		for (j = 0; j < IF_SENTINEL; j++) {
			my_sc.out_dram_data[j][i](dram_data_r[j][i]);
			my_sc_test.in_dram_data[j][i](dram_data_r[j][i]);
		}

	}

	my_sc.elaborate();

	sc_start(70, SC_NS);
}
