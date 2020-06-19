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

#include "compute/control/RegFile.h"
#include "model/stride_descriptor.h"
#include "util/SimdTest.h"
#include "util/defaults.h"

using namespace sc_core;
using namespace sc_dt;
using namespace compute_control;
using namespace std;
using namespace simd_test;

namespace compute_test {

/** Unit test for compute_control::RegFile_1S_3R1W.
 *
 * @todo Test coverage for SP/DRAM storage half. */
template <unsigned int THREADS, unsigned int LANES, unsigned int BUS_WIDTH,
	unsigned int BUS_WIDTH_SP>
class Test_RegFile : public SimdTest
{
public:
	/** DRAM clock, SDR */
	sc_in<bool> in_clk{"in_clk"};

	/** DRAM input clock */
	sc_in<bool> in_clk_dram{"in_clk_dram"};

	/** Read requests for this cycle */
	sc_fifo_out<reg_read_req<THREADS/LANES> > out_req_r{"out_req_r"};

	/** Data out for read operations */
	sc_in<sc_uint<32> > in_data_r[3][LANES];

	/** Bank conflicts for read ops. */
	sc_fifo_in<sc_bv<3> > in_req_conflicts{"in_req_conflicts"};

	/** Write request */
	sc_inout<Register<THREADS/LANES> > out_req_w{"out_req_w"};

	/** Data in for write operations */
	sc_inout<sc_uint<32> > out_data_w[LANES];

	/** Mask determining which registers should be written. */
	sc_inout<sc_bv<LANES> > out_mask_w{"out_mask_w"};

	/** Perform an actual write. */
	sc_inout<bool> out_w{"out_w"};

	/** Last warp executing. Used for determining in_thread_active. */
	sc_inout<sc_uint<const_log2(THREADS/LANES)> > out_last_warp[2];

	/** Workgroup associated with write mask. */
	sc_inout<sc_uint<1> > out_wg_mask_w{"out_wg_mask_w"};

	/** Column for reading the special mask registers */
	sc_fifo_out<sc_uint<const_log2(THREADS/LANES)> >
				out_col_mask_w{"out_col_mask_w"};

	/** Mask results */
	sc_in<sc_bv<LANES> > in_mask_w{"in_mask_w"};

	/** Boolean: set to true iff mask should be ignored for write operation.
	 * Used for CPOP. */
	sc_inout<bool> out_ignore_mask_w{"out_ignore_mask_w"};

	/** Thread active */
	sc_in<sc_bv<2> > in_thread_active{"in_thread_active"};

	/** Workgroup has finished execution */
	sc_in<sc_bv<2> > in_wg_finished{"in_wg_finished"};

	/***************** Write channel for "inactive" WG *****************/
	/** Reset CMASK for given workgroup */
	sc_inout<bool> out_cmask_rst{"in_cmask_rst"};

	/** Workgroup to reset CMASK for */
	sc_inout<sc_uint<1> > out_cmask_rst_wg{"in_cmask_rst_wg"};

	/** Workgroup offsets (X, Y) */
	sc_inout<sc_uint<32> > out_wg_off[2][2];

	/** Workgroup dimensions (X, Y) */
	sc_inout<sc_uint<32> > out_dim[2];

	/** Dimension of the workgroups */
	sc_inout<workgroup_width> out_wg_width{"out_wg_width"};

	/************** Write channel from Storage sources *****************/
	/** Data bus enabled. */
	sc_inout<bool> out_store_enable[IF_SENTINEL];

	/** Operation is a Register write op. */
	sc_inout<bool> out_store_write[IF_SENTINEL];

	/** Register description of (first) data word element */
	sc_inout<AbstractRegister> out_store_reg[IF_SENTINEL];

	/** Write mask. */
	sc_inout<sc_bv<BUS_WIDTH/4> > out_dram_store_mask{"out_dram_store_mask"};

	/** Indexes for each incoming data word */
	sc_inout<reg_offset_t<THREADS> > out_dram_store_idx[BUS_WIDTH/4];

	/** Data from different storage systems (SP, DRAM). */
	sc_in<sc_uint<32> > out_dram_store_data[BUS_WIDTH/4];

	/** Outgoing data to DRAM */
	sc_in<sc_uint<32> > in_dram_store_data[BUS_WIDTH/4];

	/** Write mask taking into account individual lane status */
	sc_in<sc_bv<BUS_WIDTH/4> > in_dram_store_mask{"in_dram_store_mask"};

	/** Write mask. */
	sc_inout<sc_bv<BUS_WIDTH_SP> > out_sp_store_mask[2];

	/** Indexes for each incoming data word */
	sc_inout<reg_offset_t<THREADS> > out_sp_store_idx[2][BUS_WIDTH_SP];

	/** Data from different storage systems (SP, DRAM). */
	sc_in<sc_uint<32> > out_sp_store_data[2][BUS_WIDTH_SP];

	/** Outgoing data to DRAM */
	sc_in<sc_uint<32> > in_sp_store_data[2][BUS_WIDTH_SP];

	/** Write mask taking into account individual lane status */
	sc_in<sc_bv<BUS_WIDTH_SP> > in_sp_store_mask[2];

	/** Trigger an index push. */
	sc_inout<bool> out_store_idx_push_trigger{"out_store_idx_push_trigger"};

	/** Providing indexes to the StrideSequencers index iterator. */
	sc_fifo_in<idx_t<THREADS> > in_store_idx{"in_store_idx"};

	/*********************** DRAM specific **************************/
	/** Destination targeted by DRAM request. */
	sc_inout<RequestTarget> out_dram_dst{"in_dram_dst"};

	/**************** Stride-pattern special registers*************/
	/** Stride-descriptor special register values.
	 * @todo Wasteful in terms of memory. Also, if we convey offsets as
	 * (x,y) pairs rather than a single counter, we might need a dedicated
	 * struct. */
	sc_in<stride_descriptor> in_sd[2];

	/** Construct test thread */
	SC_CTOR(Test_RegFile)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();

		SC_THREAD(thread_conflicts);
		sensitive << in_clk.pos();
	}
private:
	void
	set_work_params(workgroup_width w, unsigned int dim_x,
			unsigned int dim_y)
	{
		out_wg_width.write(w);
		out_dim[0].write(dim_x);
		out_dim[1].write(dim_y);
	}

	void
	set_wg_params(unsigned int wg, unsigned int off_x, unsigned int off_y)
	{

		out_wg_off[wg][0].write(off_x);
		out_wg_off[wg][1].write(off_y);

	}

	/** Test the values for the ssp reads */
	void
	test_ssp(void)
	{
		reg_read_req<THREADS/LANES> req;
		unsigned int l;

		set_work_params(WG_WIDTH_1024, 1920, 1080);

		set_wg_params(0, 32, 0);
		set_wg_params(1, 32, 1);

		req.r = 0;
		req.r[0] = true;
		req.r[1] = true;
		req.reg[0] = Register<THREADS/LANES>(0, REGISTER_SSP, SSP_WG_OFF_X, 0);
		req.reg[1] = Register<THREADS/LANES>(0, REGISTER_SSP, SSP_WG_OFF_Y, 0);
		out_req_r.write(req);
		wait(SC_ZERO_TIME);
		wait();

		for (l = 0; l < LANES; l++) {
			assert(in_data_r[0][l].read() == 1024);
			assert(in_data_r[1][l].read() == 0);
		}

		req.reg[0] = Register<THREADS/LANES>(1, REGISTER_SSP, SSP_WG_OFF_X, 0);
		req.reg[1] = Register<THREADS/LANES>(1, REGISTER_SSP, SSP_WG_OFF_Y, 0);
		out_req_r.write(req);
		wait(SC_ZERO_TIME);
		wait();

		for (l = 0; l < LANES; l++) {
			assert(in_data_r[0][l].read() == 1024);
			assert(in_data_r[1][l].read() == 1);
		}

		req.reg[0] = Register<THREADS/LANES>(1, REGISTER_SSP, SSP_DIM_X, 0);
		req.reg[1] = Register<THREADS/LANES>(1, REGISTER_SSP, SSP_DIM_Y, 0);
		out_req_r.write(req);
		wait(SC_ZERO_TIME);
		wait();

		for (l = 0; l < LANES; l++) {
			assert(in_data_r[0][l].read() == 1920);
			assert(in_data_r[1][l].read() == 1080);
		}

		set_wg_params(0, 8, 5);
		set_wg_params(1, 12, 1);

		req.reg[0] = Register<THREADS/LANES>(0, REGISTER_SSP, SSP_WG_OFF_X, 0);
		req.reg[1] = Register<THREADS/LANES>(0, REGISTER_SSP, SSP_WG_OFF_Y, 0);
		out_req_r.write(req);
		wait(SC_ZERO_TIME);
		wait();

		for (l = 0; l < LANES; l++) {
			assert(in_data_r[0][l].read() == 256);
			assert(in_data_r[1][l].read() == 5);
		}

		req.reg[0] = Register<THREADS/LANES>(1, REGISTER_SSP, SSP_WG_OFF_X, 0);
		req.reg[1] = Register<THREADS/LANES>(1, REGISTER_SSP, SSP_WG_OFF_Y, 0);
		out_req_r.write(req);
		wait(SC_ZERO_TIME);
		wait();

		for (l = 0; l < LANES; l++) {
			assert(in_data_r[0][l].read() == 384);
			assert(in_data_r[1][l].read() == 1);
		}
	}

	/** Test some vsp registers */
	void
	test_vsp(void)
	{
		reg_read_req<THREADS/LANES> req;
		unsigned int l;
		unsigned int c;

		set_work_params(WG_WIDTH_1024, 1920, 1080);

		/** Test the "simple" case for thread IDs */
		set_wg_params(0, 32, 0);
		set_wg_params(1, 0, 1);

		req.r = 0;
		req.r[0] = true;
		req.r[1] = true;
		for (c = 0; c < THREADS/LANES; c++) {
			req.reg[0] = Register<THREADS/LANES>(0, REGISTER_VSP, VSP_TID_X, c);
			req.reg[1] = Register<THREADS/LANES>(0, REGISTER_VSP, VSP_TID_Y, c);
			out_req_r.write(req);
			wait(SC_ZERO_TIME);
			wait();

			for (l = 0; l < LANES; l++) {
				assert(in_data_r[0][l].read() == 1024 + (c * LANES) + l);
				assert(in_data_r[1][l].read() == 0);
			}
		}

		for (c = 0; c < THREADS/LANES; c++) {
			req.reg[0] = Register<THREADS/LANES>(1, REGISTER_VSP, VSP_TID_X, c);
			req.reg[1] = Register<THREADS/LANES>(1, REGISTER_VSP, VSP_TID_Y, c);
			out_req_r.write(req);
			wait(SC_ZERO_TIME);
			wait();

			for (l = 0; l < LANES; l++) {
				assert(in_data_r[0][l].read() == l + (c * LANES));
				assert(in_data_r[1][l].read() == 1);
			}
		}

		/** A more elaborate case */
		set_work_params(WG_WIDTH_256, 1920, 1080);
		set_wg_params(0, 32, 8);

		for (c = 0; c < THREADS/LANES; c++) {
			req.reg[0] = Register<THREADS/LANES>(0, REGISTER_VSP, VSP_TID_X, c);
			req.reg[1] = Register<THREADS/LANES>(0, REGISTER_VSP, VSP_TID_Y, c);
			out_req_r.write(req);
			wait(SC_ZERO_TIME);
			wait();

			for (l = 0; l < LANES; l++) {
				assert(in_data_r[0][l].read() == 1024 + ((c * LANES) + l) % 256);
				assert(in_data_r[1][l].read() == 8 + ((c * LANES) + l)/ 256);
			}
		}

		set_work_params(WG_WIDTH_64, 1920, 1080);
		set_wg_params(1, 6, 32);

		for (c = 0; c < THREADS/LANES; c++) {
			req.reg[0] = Register<THREADS/LANES>(1, REGISTER_VSP, VSP_TID_X, c);
			req.reg[1] = Register<THREADS/LANES>(1, REGISTER_VSP, VSP_TID_Y, c);
			out_req_r.write(req);
			wait(SC_ZERO_TIME);
			wait();

			for (l = 0; l < LANES; l++) {
				assert(in_data_r[0][l].read() == 192 + ((c * LANES) + l) % 64);
				assert(in_data_r[1][l].read() == 32 + ((c * LANES) + l)/ 64);
			}
		}
	}

	/** @todo This is obviously not a very helpful thread for testing. */
	void
	thread_conflicts(void)
	{
		sc_bv<3> conflicts;

		while(true)
		{
			in_req_conflicts.read(conflicts);
		}
	}

	/** Main thread */
	void
	thread_lt(void)
	{
		int i;
		sc_bv<LANES> bv;
		reg_read_req<THREADS/LANES> req;
		Register<THREADS/LANES> wreq;

		/* Cycle 0: reset behaviour, disable write.
		 * @todo Make this reset behaviour part of IExecute */
		out_last_warp[0].write((THREADS/LANES)-1);
		out_last_warp[1].write((THREADS/LANES)-1);
		out_store_enable[IF_DRAM].write(false);
		out_store_enable[IF_SP_WG0].write(false);
		out_store_enable[IF_SP_WG1].write(false);
		out_dram_dst.write(RequestTarget(0,TARGET_REG));
		out_mask_w.write(0);
		wreq.type = REGISTER_VGPR;
		out_w.write(false);
		out_req_w.write(wreq);
		wait();

		req.r = 1;
		req.reg[0].wg = 0;
		req.reg[0].type = REGISTER_VGPR;
		out_req_r.write(req);
		wait();

		/* Cycle 1: Write lane number to VGPR 0 for column/subwarp 0
		 * 	    read VGPR0 for subwarp 7. */
		for (i = 0; i < LANES; i++) {
			out_data_w[i].write(i);
		}

		wreq.col = 0;
		wreq.row = 0;
		bv = 0;
		out_mask_w.write(bv.b_not());
		out_w.write(true);
		out_req_w.write(wreq);

		req.reg[0].type = REGISTER_VGPR;
		req.reg[0].col = 7;
		req.reg[0].row = 0;
		out_req_r.write(req);
		wait();

		/* Cycle 2: Write the same succession to VGPR0 subwarp 7
		 * 	    Read back VGPR0 subwarp 0 */
		req.reg[0].col = 0;
		req.reg[0].row = 0;
		wreq.col = 7;
		out_req_r.write(req);
		out_req_w.write(wreq);
		wait();

		/* Cycle 3: Write 0 to half the lanes of VGPR0 subwarp 1*/
		req.r = 0;
		for (i = 0; i < LANES; i++) {
			out_data_w[i].write(0);
			bv[i] = ((i % 2) == 0);
		}
		out_mask_w.write(bv);
		out_req_r.write(req);
		wait();

		/* Cycle 4: Write a value to the first SGPR */
		wreq.type = REGISTER_SGPR;
		out_mask_w.write(1);
		wreq.row = 0;
		out_data_w[0].write(42);
		out_req_r.write(req);
		out_req_w.write(wreq);
		wait();

		/* Cycle 5: write to PR */
		bv = 0;
		out_mask_w.write(bv.b_not());
		wreq.row = 3;
		wreq.col = 7;
		wreq.type = REGISTER_PR;
		for (i = 0; i < LANES; i++) {
			out_data_w[i].write(i%2);
		}
		req.r = 0;
		out_req_r.write(req);
		out_req_w.write(wreq);
		wait();

		/* Cycle 6: disable write, read from all sorts of channels. */
		for (i = 0; i < LANES; i++) {
			assert(in_data_r[0][i].read() == i);
		}
		assert(in_thread_active.read()[0]);

		out_mask_w.write(0);
		out_w.write(false);
		req.r = 7;

		req.reg[0].type = REGISTER_VGPR;
		req.reg[0].row = 0;
		req.reg[0].col = 7;

		req.reg[1].type = REGISTER_PR;
		req.reg[1].row = 3;
		req.reg[1].col = 7;

		req.reg[2].type = REGISTER_SGPR;
		req.reg[2].row = 0;
		req.reg[2].col = 7;

		out_req_r.write(req);
		wait();

		/* Cycle 7: Lots of data incoming */
		for (i = 0; i < LANES; i++) {
			assert(in_data_r[0][i].read() == ((i % 2) == 0 ? 0 : i));
			assert(in_data_r[1][i].read() == (i % 2));
			assert(in_data_r[2][i].read() == 42);
		}
		assert(in_thread_active.read()[0]);

		req.reg[1].type = REGISTER_IMM;
		req.imm[1] = 0xdeadbeef;
		req.reg[1].col = 1;
		req.reg[1].row = 0;
		req.r = 3;
		out_req_r.write(req);

		wait();

		/* Cycle 8: read the immediate value requested for broadcast */
		for (i = 0; i < LANES; i++) {
			assert(in_data_r[1][i].read() == 0xdeadbeef);
		}
		assert(in_thread_active.read()[0]);

		/* Let's go and disable all threads through various means. */
		bv = 0;
		bv.b_not();
		for (i = 0; i < LANES; i++)
			out_data_w[i].write(0);
		req.r = 0;
		out_req_r.write(req);
		out_mask_w.write(bv);
		wreq.type = REGISTER_VSP;
		out_dram_dst.write(RequestTarget(0,TARGET_CAM));

		for (i = 0; i < THREADS/LANES; i++) {
			assert(in_thread_active.read()[0]);

			wreq.col = i;
			wreq.row = i % 4;
			out_w.write(true);
			out_req_w.write(wreq);

			wait();
		}
		out_w.write(false);

		wait();
		assert(!in_thread_active.read()[0]);
		wreq.wg = 1;
		out_req_w.write(wreq);

		wait();
		wait();
		assert(in_thread_active.read()[1]);
		wreq.wg = 0;
		out_req_w.write(wreq);
		wait();

		out_cmask_rst.write(1);
		out_cmask_rst_wg.write(0);
		wait();
		assert(!in_thread_active.read()[0]);
		out_cmask_rst.write(0);
		wait();
		assert(in_thread_active.read()[0]);

		test_ssp();
		test_vsp();

		test_finish();
	}
};

}

using namespace compute_control;
using namespace compute_test;

int
sc_main(int argc, char* argv[])
{
	sc_signal<bool> rst;
	sc_fifo<reg_read_req<COMPUTE_THREADS/COMPUTE_FPUS> > req(1);
	sc_signal<sc_uint<32> > data_r[3][COMPUTE_FPUS];
	sc_fifo<sc_bv<3> > req_conflicts(1);
	sc_signal<Register<COMPUTE_THREADS/COMPUTE_FPUS> > req_w;
	sc_signal<RegisterType> type_w;
	sc_signal<sc_uint<const_log2(COMPUTE_THREADS/COMPUTE_FPUS)> > col_w;
	sc_signal<sc_uint<const_log2(64)> > row_w;
	sc_signal<sc_uint<32> > data_w[COMPUTE_FPUS];
	sc_signal<sc_bv<COMPUTE_FPUS> > mask_w;
	sc_signal<sc_uint<1> > wg_mask_w;
	sc_fifo<sc_uint<const_log2(COMPUTE_THREADS/COMPUTE_FPUS)> > col_mask_w(1);
	sc_signal<sc_bv<COMPUTE_FPUS> > o_mask_w;
	sc_signal<bool> w;
	sc_signal<sc_bv<2> > thread_active;
	sc_signal<sc_bv<2> > wg_finished;
	sc_signal<bool> ignore_mask_w;
	sc_signal<sc_uint<const_log2(COMPUTE_THREADS/COMPUTE_FPUS)> > last_warp[2];
	sc_signal<bool> cmask_rst;
	sc_signal<sc_uint<1> > cmask_rst_wg;
	sc_signal<sc_uint<32> > wg_off[2][2];
	sc_signal<sc_uint<32> > dim[2];
	sc_signal<workgroup_width> wg_width;

	sc_signal<sc_uint<32> > dram_store_data_o[4];
	sc_signal<sc_uint<32> > dram_store_data[4];
	sc_signal<sc_uint<32> > store_data_o[2][SP_BUS_WIDTH];
	sc_signal<sc_uint<32> > store_data[2][SP_BUS_WIDTH];
	sc_signal<bool> store_enable[IF_SENTINEL];
	sc_signal<bool> store_write[IF_SENTINEL];
	sc_signal<AbstractRegister> store_reg[IF_SENTINEL];
	sc_signal<sc_bv<4> > dram_store_mask;
	sc_signal<sc_bv<4> > dram_store_mask_o;
	sc_signal<sc_bv<SP_BUS_WIDTH> > store_mask[2];
	sc_signal<sc_bv<SP_BUS_WIDTH> > store_mask_o[2];
	sc_signal<bool> store_idx_push_trigger;
	sc_fifo<idx_t<COMPUTE_THREADS> > o_store_idx;
	sc_signal<reg_offset_t<COMPUTE_THREADS> > dram_store_idx[MC_BUS_WIDTH/4];
	sc_signal<reg_offset_t<COMPUTE_THREADS> > store_idx[2][SP_BUS_WIDTH];
	sc_signal<RequestTarget> dram_dst;
	sc_signal<stride_descriptor> sd[2];

	sc_clock clk("clk", sc_time(10./12., SC_NS));
	sc_clock clk_dram("clk_dram", sc_time(10./16., SC_NS));

	RegFile<COMPUTE_THREADS,COMPUTE_FPUS,MC_BUS_WIDTH,SP_BUS_WIDTH> my_regfile("my_regfile");
	my_regfile.in_clk(clk);
	my_regfile.in_clk_dram(clk_dram);
	my_regfile.in_req_r(req);
	my_regfile.out_req_conflicts(req_conflicts);
	my_regfile.in_req_w(req_w);
	my_regfile.in_mask_w(mask_w);
	my_regfile.in_w(w);
	my_regfile.in_last_warp[0](last_warp[0]);
	my_regfile.in_last_warp[1](last_warp[1]);
	my_regfile.in_wg_mask_w(wg_mask_w);
	my_regfile.in_col_mask_w(col_mask_w);
	my_regfile.out_mask_w(o_mask_w);
	my_regfile.in_ignore_mask_w(ignore_mask_w);
	my_regfile.out_thread_active(thread_active);
	my_regfile.out_wg_finished(wg_finished);
	my_regfile.in_cmask_rst(cmask_rst);
	my_regfile.in_cmask_rst_wg(cmask_rst_wg);
	my_regfile.in_wg_off[0][0](wg_off[0][0]);
	my_regfile.in_wg_off[0][1](wg_off[0][1]);
	my_regfile.in_wg_off[1][0](wg_off[1][0]);
	my_regfile.in_wg_off[1][1](wg_off[1][1]);
	my_regfile.in_dim[0](dim[0]);
	my_regfile.in_dim[1](dim[1]);
	my_regfile.in_wg_width(wg_width);
	my_regfile.in_dram_store_mask(dram_store_mask);
	my_regfile.out_dram_store_mask(dram_store_mask_o);

	for (unsigned int i = 0; i < 2; i++) {
		my_regfile.in_sp_store_mask[i](store_mask[i]);
		my_regfile.out_sp_store_mask[i](store_mask_o[i]);
	}
	for (unsigned int i = 0; i < IF_SENTINEL; i++) {
		my_regfile.in_store_enable[i](store_enable[i]);
		my_regfile.in_store_write[i](store_write[i]);
		my_regfile.in_store_reg[i](store_reg[i]);
	}
	my_regfile.in_store_idx_push_trigger(store_idx_push_trigger);
	my_regfile.out_store_idx(o_store_idx);
	my_regfile.in_dram_dst(dram_dst);
	my_regfile.out_sd[0](sd[0]);
	my_regfile.out_sd[1](sd[1]);

	Test_RegFile<COMPUTE_THREADS,COMPUTE_FPUS,MC_BUS_WIDTH,SP_BUS_WIDTH> my_regfile_test("my_regfile_test");
	my_regfile_test.in_clk(clk);
	my_regfile_test.in_clk_dram(clk_dram);
	my_regfile_test.out_req_r(req);
	my_regfile_test.in_req_conflicts(req_conflicts);
	my_regfile_test.out_req_w(req_w);
	my_regfile_test.out_mask_w(mask_w);
	my_regfile_test.out_w(w);
	my_regfile_test.out_last_warp[0](last_warp[0]);
	my_regfile_test.out_last_warp[1](last_warp[1]);
	my_regfile_test.out_wg_mask_w(wg_mask_w);
	my_regfile_test.out_col_mask_w(col_mask_w);
	my_regfile_test.in_mask_w(o_mask_w);
	my_regfile_test.out_ignore_mask_w(ignore_mask_w);
	my_regfile_test.in_thread_active(thread_active);
	my_regfile_test.in_wg_finished(wg_finished);
	my_regfile_test.out_cmask_rst(cmask_rst);
	my_regfile_test.out_cmask_rst_wg(cmask_rst_wg);
	my_regfile_test.out_wg_off[0][0](wg_off[0][0]);
	my_regfile_test.out_wg_off[0][1](wg_off[0][1]);
	my_regfile_test.out_wg_off[1][0](wg_off[1][0]);
	my_regfile_test.out_wg_off[1][1](wg_off[1][1]);
	my_regfile_test.out_dim[0](dim[0]);
	my_regfile_test.out_dim[1](dim[1]);
	my_regfile_test.out_wg_width(wg_width);
	for (unsigned int i = 0; i < int(IF_SENTINEL); i++) {
		my_regfile_test.out_store_enable[i](store_enable[i]);
		my_regfile_test.out_store_write[i](store_write[i]);
		my_regfile_test.out_store_reg[i](store_reg[i]);
	}
	my_regfile_test.out_dram_store_mask(dram_store_mask);
	my_regfile_test.in_dram_store_mask(dram_store_mask_o);
	my_regfile_test.out_sp_store_mask[0](store_mask[0]);
	my_regfile_test.in_sp_store_mask[0](store_mask_o[0]);
	my_regfile_test.out_sp_store_mask[1](store_mask[1]);
	my_regfile_test.in_sp_store_mask[1](store_mask_o[1]);
	my_regfile_test.out_store_idx_push_trigger(store_idx_push_trigger);
	my_regfile_test.in_store_idx(o_store_idx);
	my_regfile_test.out_dram_dst(dram_dst);
	my_regfile_test.in_sd[0](sd[0]);
	my_regfile_test.in_sd[1](sd[1]);

	for (unsigned int p = 0; p < 3; p++) {
		for (unsigned int i = 0; i < COMPUTE_FPUS; i++) {
			my_regfile.out_data_r[p][i](data_r[p][i]);
			my_regfile_test.in_data_r[p][i](data_r[p][i]);
		}
	}

	for (unsigned int i = 0; i < COMPUTE_FPUS; i++) {
		my_regfile.in_data_w[i](data_w[i]);
		my_regfile_test.out_data_w[i](data_w[i]);
	}

	for (unsigned int i = 0; i < MC_BUS_WIDTH/4; i++) {
			my_regfile.in_dram_store_data[i](dram_store_data[i]);
			my_regfile.out_dram_store_data[i](dram_store_data_o[i]);
			my_regfile.in_dram_store_idx[i](dram_store_idx[i]);

			my_regfile_test.out_dram_store_data[i](dram_store_data[i]);
			my_regfile_test.in_dram_store_data[i](dram_store_data_o[i]);
			my_regfile_test.out_dram_store_idx[i](dram_store_idx[i]);
	}

	for (unsigned int i = 0; i < SP_BUS_WIDTH; i++) {
		for (unsigned int j = 0; j < 2; j++) {
			my_regfile.in_sp_store_data[j][i](store_data[j][i]);
			my_regfile.out_sp_store_data[j][i](store_data_o[j][i]);
			my_regfile.in_sp_store_idx[j][i](store_idx[j][i]);

			my_regfile_test.out_sp_store_data[j][i](store_data[j][i]);
			my_regfile_test.in_sp_store_data[j][i](store_data_o[j][i]);
			my_regfile_test.out_sp_store_idx[j][i](store_idx[j][i]);
		}
	}

	sc_core::sc_start(700, sc_core::SC_NS);

	assert(my_regfile_test.has_finished());

	return 0;
}
