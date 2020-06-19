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
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <cstdio>
#include <list>

#include "util/defaults.h"

#include "compute/control/WorkScheduler.h"
#include "compute/control/SimdCluster.h"
#include "compute/model/compute_stats.h"

using namespace compute_model;
using namespace compute_control;
using namespace std;

namespace compute_test {

static const bfloat f1 = {.f = 1.f};
static const bfloat f3 = {.f = 3.f};
static const bfloat f4 = {.f = 4.f};

static Instruction prg[] {
		Instruction(OP_CVT, {.cvt = CVT_I2F}, Operand(REGISTER_VGPR, 0), Operand(REGISTER_VSP, VSP_TID_X)),
		Instruction(OP_ADD, {}, Operand(REGISTER_VGPR, 0), Operand(REGISTER_VGPR, 0), Operand(f1.b)),
		Instruction(OP_CPUSH, {.cpush = CPUSH_IF}, Operand(), Operand(9)),
		Instruction(OP_BRA, {}, Operand(), Operand(7), Operand(REGISTER_PR, 0)),
		Instruction(OP_MUL, {}, Operand(REGISTER_VGPR, 0), Operand(REGISTER_VGPR, 4), Operand(f3.b)),
		Instruction(OP_MAD, {}, Operand(REGISTER_VGPR, 0), Operand(REGISTER_VGPR, 4), Operand(f4.b), Operand(REGISTER_VGPR, 0)),
		Instruction(OP_CPOP, {}),
		Instruction(OP_MAD, {}, Operand(REGISTER_VGPR, 0), Operand(REGISTER_VGPR, 4), Operand(f4.b), Operand(REGISTER_VGPR, 0)),
		Instruction(OP_CPOP, {}),
		Instruction(OP_EXIT, {})
};

template <unsigned int XLAT_ENTRIES = 32>
class Test_compute : public sc_core::sc_module
{
public:
	/** Clock input */
	sc_in<bool> in_clk;

	/** Reset. */
	sc_inout<bool> out_rst;

	/** Test program to execute. */
	sc_inout<work<XLAT_ENTRIES> > out_work;

	/** Kick-off workscheduler and pipeline. */
	sc_inout<bool> out_kick;

	SC_CTOR(Test_compute)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();
	}

	void
	thread_lt(void)
	{
		work<XLAT_ENTRIES> program;
		unsigned int i;
		const unsigned int prg_size = (sizeof(prg) / sizeof(prg[0]));

		for (i = 0; i < prg_size; i++)
			program.add_op(prg[i]);

		program.dims[0] = 1920;
		program.dims[1] = 32;
		program.wg_width = WG_WIDTH_1024;

		out_rst.write(false);
		out_work.write(program);
		out_kick.write(true);

		wait();
		out_kick.write(false);
	}
};

}

using namespace compute_control;
using namespace compute_test;
using namespace sc_core;
using namespace sc_dt;

/* SystemC: Full system */
static sc_clock clk("clk", sc_time(1.L, SC_NS));
static sc_clock clk_dram("clk_dram", sc_time(10./16., SC_NS));
static int ns = 5200;

static sc_signal<bool> rst;

static Test_compute<MC_BIND_BUFS> test("test");
static WorkScheduler<COMPUTE_THREADS,COMPUTE_FPUS,11,MC_BIND_BUFS> workscheduler("workscheduler");
static SimdCluster<COMPUTE_THREADS,COMPUTE_FPUS,COMPUTE_RCPUS,COMPUTE_PC_WIDTH,MC_BIND_BUFS,MC_BUS_WIDTH,SP_BUS_WIDTH> simdcluster("simdcluster");

/* WorkScheduler -> ??? */
static sc_signal<work<MC_BIND_BUFS> > test_work;
static sc_signal<bool> test_kick;
static sc_fifo<workgroup<COMPUTE_THREADS,COMPUTE_FPUS> > workscheduler_wg(1);

/* WorkScheduler -> RegFile */
static sc_signal<sc_uint<32> > workscheduler_dim[2];
static sc_signal<workgroup_width> workscheduler_wg_width;

/* WorkScheduler -> IMem */
static sc_signal<Instruction> workscheduler_op_w[2];
static sc_signal<sc_uint<COMPUTE_PC_WIDTH> > workscheduler_pc_w;
static sc_signal<bool> workscheduler_w;

static sc_signal<bool> workscheduler_xlat_w;
static sc_signal<sc_uint<const_log2(MC_BIND_BUFS)> > workscheduler_xlat_idx_w;
static sc_signal<Buffer> workscheduler_xlat_phys_w;

/* WorkScheduler -> SimdCluster */
static sc_signal<bool> workscheduler_end_prg;

/* SimdCluster -> WorkScheduler */
static sc_signal<bool> simdcluster_exec_fini;

void
elaborate()
{
	/* WorkScheduler */
	workscheduler.in_clk(clk);
	workscheduler.in_work(test_work);
	workscheduler.in_kick(test_kick);
	workscheduler.out_wg(workscheduler_wg);
	workscheduler.out_imem_op[0](workscheduler_op_w[0]);
	workscheduler.out_imem_op[1](workscheduler_op_w[1]);
	workscheduler.out_imem_pc(workscheduler_pc_w);
	workscheduler.out_imem_w(workscheduler_w);
	workscheduler.out_wg_width(workscheduler_wg_width);
	workscheduler.out_dim[0](workscheduler_dim[0]);
	workscheduler.out_dim[1](workscheduler_dim[1]);
	workscheduler.out_end_prg(workscheduler_end_prg);
	workscheduler.in_exec_fini(simdcluster_exec_fini);
	workscheduler.out_xlat_w(workscheduler_xlat_w);
	workscheduler.out_xlat_idx_w(workscheduler_xlat_idx_w);
	workscheduler.out_xlat_phys_w(workscheduler_xlat_phys_w);

	/* SimdCluster */
	simdcluster.in_clk(clk);
	simdcluster.in_clk_dram(clk_dram);
	simdcluster.in_rst(rst);
	simdcluster.in_wg(workscheduler_wg);
	simdcluster.in_work_dim[0](workscheduler_dim[0]);
	simdcluster.in_work_dim[1](workscheduler_dim[1]);
	simdcluster.in_wg_width(workscheduler_wg_width);
	simdcluster.in_prog_op_w[0](workscheduler_op_w[0]);
	simdcluster.in_prog_op_w[1](workscheduler_op_w[1]);
	simdcluster.in_prog_pc_w(workscheduler_pc_w);
	simdcluster.in_prog_w(workscheduler_w);
	simdcluster.in_end_prg(workscheduler_end_prg);
	simdcluster.out_exec_fini(simdcluster_exec_fini);
	simdcluster.in_xlat_w(workscheduler_xlat_w);
	simdcluster.in_xlat_idx_w(workscheduler_xlat_idx_w);
	simdcluster.in_xlat_phys_w(workscheduler_xlat_phys_w);

	simdcluster.elaborate();

	/* Test driver */
	test.in_clk(clk);
	test.out_rst(rst);
	test.out_work(test_work);
	test.out_kick(test_kick);
}

/** Execute simulation
 */
void
do_sim()
{
	compute_stats s;

	/* Run */
	sc_start(ns, sc_core::SC_NS);

	workscheduler.get_stats(s);

	cout << endl;
	cout << s;
}

/** Main SystemC thread.
 * @param argc Number of strings in argv.
 * @param argv Command-line parameters.
 * @return 0 iff program ended successfully.
 */
int
sc_main(int argc, char* argv[])
{
	int pid;

	/* Now that test is set up correctly, we can start parsing params
	parse_parameters(argc, argv, &descs); */

	elaborate();
	//stats = allocate_stats();

	pid = fork();
	if (pid == 0) {
		do_sim();
		exit(0);
	}

	waitpid(pid, NULL, 0);

	//}

	/* Print aggregates */
	//print_aggregate_stats(stats);

	//free_stats(stats);

	return 0;
}
