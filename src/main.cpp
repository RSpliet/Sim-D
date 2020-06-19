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
#include <iostream>
#include <string>
#include <array>

#include "mc/control/Backend.h"
#include "mc/control/StrideSequencer.h"
#include "compute/control/WorkScheduler.h"
#include "compute/control/SimdCluster.h"
#include "util/constmath.h"
#include "util/defaults.h"
#include "util/sched_opts.h"
#include "isa/model/Program.h"
#include "isa/analysis/ControlFlow.h"
#include "model/Buffer.h"
#include "model/request_target.h"

using namespace std;
using namespace sc_dt;
using namespace sc_core;
using namespace tlm;
using namespace simd_model;
using namespace mc_model;
using namespace mc_control;
using namespace compute_model;
using namespace compute_control;

static int ns = 0;
static string program = "";
static unsigned long dims[2];
static float delta = 0.001f;
static bool dfrac = false;

static IDecode_impl idec_impl = IDECODE_1S;
static unsigned int iexec_pipe_length = 3;

static sc_bv<WSS_SENTINEL> ws_sched = 0;
static unsigned long refc = 0;

static Program prg;

typedef struct {
	enum {
		ACTION_DOWNLOAD,
		ACTION_COMPARE,
	} action;
	string path;
	unsigned int buffer;
	buffer_input_type type;
} download;

typedef struct {
	string path;
	unsigned int buffer;
	buffer_input_type type;
} upload;

static vector<download> d;
static vector<upload> u;

namespace simd_test {

/** Generator of SimD control signals. */
template <unsigned int XLAT_ENTRIES = MC_BIND_BUFS>
class SimD_Control : public sc_module
{
private:
	workgroup_width wgw;

	void
	prg_set_wg_width(work<XLAT_ENTRIES> &p)
	{
		if (wgw < WG_WIDTH_SENTINEL)
			p.wg_width = wgw;
		else if (dims[0] >= 1024)
			p.wg_width = WG_WIDTH_1024;
		else if (dims[0] >= 512)
			p.wg_width = WG_WIDTH_512;
		else if (dims[0] >= 256)
			p.wg_width = WG_WIDTH_256;
		else if (dims[0] >= 128)
			p.wg_width = WG_WIDTH_128;
		else if (dims[0] >= 64)
			p.wg_width = WG_WIDTH_64;
		else
			p.wg_width = WG_WIDTH_32;

	}

public:
	/** Clock input */
	sc_in<bool> in_clk{"in_clk"};

	/** Reset. */
	sc_inout<bool> out_rst{"out_rst"};

	/** Test program to execute. */
	sc_inout<work<XLAT_ENTRIES> > out_work{"out_work"};

	/** Kick-off workscheduler and pipeline. */
	sc_inout<bool> out_kick{"out_kick"};

	SC_CTOR(SimD_Control) : wgw(WG_WIDTH_SENTINEL)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();
	}

	void
	set_workgroup_width(workgroup_width w)
	{
		wgw = w;
	}

	void
	thread_lt(void)
	{
		work<XLAT_ENTRIES> program;
		const ProgramBuffer *b;
		vector<Instruction *> v;

		v = prg.linearise_code();
		prg.validate_buffers();

		for (Instruction *in : v)
			program.add_op(*in);

		program.set_sched_options(ws_sched);

		for (b = prg.buffer_begin(); b < prg.buffer_end(); b++) {
			program.add_buf(*b);
		}

		for (b = prg.sp_buffer_begin(); b < prg.sp_buffer_end(); b++) {
			program.add_sp_buf(*b);
		}

		program.dims[0] = dims[0];
		program.dims[1] = dims[1];

		prg_set_wg_width(program);

		out_rst.write(false);
		out_work.write(program);
		out_kick.write(true);

		wait();
		out_kick.write(false);
	}
};

}

using namespace simd_test;
using namespace isa_analysis;

static sc_clock clk_compute("clk_compute", sc_time(1.L, SC_NS));
static sc_clock *clk_dram;

static sc_signal<bool> rst;

static SimD_Control<MC_BIND_BUFS> test("test");
static mc_control::StrideSequencer<MC_BUS_WIDTH,COMPUTE_THREADS> sseq("sseq");
static Backend<MC_DRAM_BANKS,MC_DRAM_COLS,MC_DRAM_ROWS> mc("mc");
static WorkScheduler<COMPUTE_THREADS,COMPUTE_FPUS,COMPUTE_PC_WIDTH,MC_BIND_BUFS>
				workscheduler("workscheduler");
static SimdCluster<COMPUTE_THREADS,COMPUTE_FPUS,COMPUTE_RCPUS,COMPUTE_PC_WIDTH,
	MC_BIND_BUFS,MC_BUS_WIDTH,SP_BUS_WIDTH> simdcluster("simdcluster");

/** Test -> WorkScheduler */
static sc_signal<work<MC_BIND_BUFS> > test_work;
static sc_signal<bool> test_kick;

/* WorkScheduler -> SimdCluster */
static sc_fifo<workgroup<COMPUTE_THREADS,COMPUTE_FPUS> > workscheduler_wg(1);
static sc_signal<sc_uint<32> > workscheduler_dim[2];
static sc_signal<workgroup_width> workscheduler_wg_width;
static sc_signal<Instruction> workscheduler_op_w[4];
static sc_signal<sc_uint<COMPUTE_PC_WIDTH> > workscheduler_pc_w;
static sc_signal<bool> workscheduler_w;
static sc_signal<bool> workscheduler_xlat_w;
static sc_signal<sc_uint<const_log2(MC_BIND_BUFS)> > workscheduler_xlat_idx_w;
static sc_signal<Buffer> workscheduler_xlat_phys_w;
static sc_signal<bool> workscheduler_sp_xlat_w;
static sc_signal<sc_uint<const_log2(MC_BIND_BUFS)> > workscheduler_sp_xlat_idx_w;
static sc_signal<Buffer> workscheduler_sp_xlat_phys_w;
static sc_signal<bool> workscheduler_end_prg;
static sc_signal<sc_bv<WSS_SENTINEL> > workscheduler_sched_opts;

/* SimdCluster -> WorkScheduler */
static sc_signal<bool> simdcluster_exec_fini;

/* SimdCluster -> StrideSequencer */
static sc_fifo<stride_descriptor> simdcluster_desc_fifo;
static sc_fifo<bool> simdcluster_dram_kick(2);
static sc_fifo<idx_t<COMPUTE_THREADS> > simdcluster_idx(16);
static sc_signal<sc_uint<4> > simdcluster_ticket_pop;

static sc_signal<sc_bv<MC_BUS_WIDTH/4> > simdcluster_dram_mask;

/* StrideSequencer -> MC Backend */
static sc_fifo<burst_request<MC_BUS_WIDTH,COMPUTE_THREADS> > sseq_req_fifo(MC_BURSTREQ_FIFO_DEPTH);

/* StrideSequencer -> SimdCluster */
static sc_signal<RequestTarget> sseq_dst;
static sc_signal<AbstractRegister> sseq_dst_reg;
static sc_signal<bool> sseq_idx_push_trigger;

/* StrideSequencer -> Test */
static sc_signal<bool> strseq_done;

/* MC Backend -> StrideSequencer */
static sc_signal<bool> mc_ref_pending;
static sc_signal<bool> mc_allpre;
static sc_signal<bool> mc_ref;
static sc_signal<long> mc_cycle;

/* MC Backend -> SimdCluster */
static sc_fifo<RequestTarget> mc_done_dst;

/* MC Backend -> SimdCluster */
static sc_signal<bool> mc_enable;
static sc_signal<reg_offset_t<COMPUTE_THREADS> > mc_vreg_idx_w[MC_BUS_WIDTH/4];

/* MC Backend -> Scratchpad */
static sc_signal<sc_uint<18> > mc_sp_addr;
static sc_signal<sc_uint<const_log2(SP_BUS_WIDTH)> > mc_sp_words;

/* MC Backend -> Scratchpad/SimdCluster */
static sc_signal<bool> mc_write;
static sc_signal<sc_bv<MC_BUS_WIDTH/4> > mc_mask_w;
static sc_signal<sc_uint<32> > mc_out_data[MC_BUS_WIDTH/4];

/* XXX: SimdCluster -> MC Backend */
static sc_signal<sc_uint<32> > simdcluster_dram_data[IF_SENTINEL][MC_BUS_WIDTH/4];

void
elaborate(void)
{
	unsigned int i;

	clk_dram = new sc_clock("clk_dram", sc_time(mc.get_clk_period(), SC_NS));

	simdcluster.setIDecode(idec_impl);

	test.in_clk(clk_compute);
	test.out_kick(test_kick);
	test.out_work(test_work);
	test.out_rst(rst);

	workscheduler.in_clk(clk_compute);
	workscheduler.in_work(test_work);
	workscheduler.in_kick(test_kick);
	workscheduler.out_wg(workscheduler_wg);
	for (i = 0; i < 4; i++)
		workscheduler.out_imem_op[i](workscheduler_op_w[i]);
	workscheduler.out_imem_pc(workscheduler_pc_w);
	workscheduler.out_imem_w(workscheduler_w);
	workscheduler.out_wg_width(workscheduler_wg_width);
	workscheduler.out_sched_opts(workscheduler_sched_opts);
	workscheduler.out_dim[0](workscheduler_dim[0]);
	workscheduler.out_dim[1](workscheduler_dim[1]);
	workscheduler.out_end_prg(workscheduler_end_prg);
	workscheduler.in_exec_fini(simdcluster_exec_fini);
	workscheduler.out_xlat_w(workscheduler_xlat_w);
	workscheduler.out_xlat_idx_w(workscheduler_xlat_idx_w);
	workscheduler.out_xlat_phys_w(workscheduler_xlat_phys_w);
	workscheduler.out_sp_xlat_w(workscheduler_sp_xlat_w);
	workscheduler.out_sp_xlat_idx_w(workscheduler_sp_xlat_idx_w);
	workscheduler.out_sp_xlat_phys_w(workscheduler_sp_xlat_phys_w);

	/* SimdCluster */
	simdcluster.in_clk(clk_compute);
	simdcluster.in_clk_dram(*clk_dram);
	simdcluster.in_rst(rst);
	simdcluster.in_wg(workscheduler_wg);
	simdcluster.in_work_dim[0](workscheduler_dim[0]);
	simdcluster.in_work_dim[1](workscheduler_dim[1]);
	simdcluster.in_wg_width(workscheduler_wg_width);
	simdcluster.in_sched_opts(workscheduler_sched_opts);
	simdcluster.out_ticket_pop(simdcluster_ticket_pop);
	simdcluster.in_prog_pc_w(workscheduler_pc_w);
	simdcluster.in_prog_w(workscheduler_w);
	simdcluster.in_end_prg(workscheduler_end_prg);
	simdcluster.out_exec_fini(simdcluster_exec_fini);
	simdcluster.in_xlat_w(workscheduler_xlat_w);
	simdcluster.in_xlat_idx_w(workscheduler_xlat_idx_w);
	simdcluster.in_xlat_phys_w(workscheduler_xlat_phys_w);
	simdcluster.in_sp_xlat_w(workscheduler_sp_xlat_w);
	simdcluster.in_sp_xlat_idx_w(workscheduler_sp_xlat_idx_w);
	simdcluster.in_sp_xlat_phys_w(workscheduler_sp_xlat_phys_w);

	simdcluster.in_dram_enable(mc_enable);
	simdcluster.in_dram_write(mc_write);
	simdcluster.in_dram_dst(sseq_dst);
	simdcluster.out_desc_fifo(simdcluster_desc_fifo);
	simdcluster.out_dram_kick(simdcluster_dram_kick);
	simdcluster.in_dram_done_dst(mc_done_dst);
	simdcluster.in_dram_mask(mc_mask_w);
	simdcluster.in_dram_reg(sseq_dst_reg);
	simdcluster.out_dram_mask(simdcluster_dram_mask);
	simdcluster.in_dram_idx_push_trigger(sseq_idx_push_trigger);
	simdcluster.out_dram_idx(simdcluster_idx);
	simdcluster.in_dram_sp_addr(mc_sp_addr);
	simdcluster.in_dram_ref(mc_ref);

	for (i = 0; i < 4; i++) {
		simdcluster.in_prog_op_w[i](workscheduler_op_w[i]);
		simdcluster.in_dram_data[i](mc_out_data[i]);
		simdcluster.out_dram_data[IF_DRAM][i](simdcluster_dram_data[IF_DRAM][i]);
		simdcluster.out_dram_data[IF_SP_WG0][i](simdcluster_dram_data[IF_SP_WG0][i]);
		simdcluster.out_dram_data[IF_SP_WG1][i](simdcluster_dram_data[IF_SP_WG1][i]);
		simdcluster.in_dram_idx[i](mc_vreg_idx_w[i]);
	}

	simdcluster.elaborate();

	/* StrideSequencer */
	sseq.in_clk(*clk_dram);
	sseq.in_desc_fifo(simdcluster_desc_fifo);
	sseq.in_trigger(simdcluster_dram_kick);
	sseq.in_ref_pending(mc_ref_pending);
	sseq.out_req_fifo(sseq_req_fifo);
	sseq.out_done(strseq_done);
	sseq.in_DQ_allpre(mc_allpre);
	sseq.out_dst(sseq_dst);
	sseq.out_dst_reg(sseq_dst_reg);
	sseq.out_idx_push_trigger(sseq_idx_push_trigger);
	sseq.in_idx(simdcluster_idx);
	sseq.in_cycle(mc_cycle);
	sseq.in_sched_opts(workscheduler_sched_opts);
	sseq.in_ticket_pop(simdcluster_ticket_pop);

	/* MC Backend */
	mc.in_clk(*clk_dram);
	mc.in_req_fifo(sseq_req_fifo);
	mc.out_ref_pending(mc_ref_pending);
	mc.out_allpre(mc_allpre);
	mc.out_ref(mc_ref);
	mc.in_mask_w(simdcluster_dram_mask);
	mc.out_sp_addr(mc_sp_addr);
	mc.out_done_dst(mc_done_dst);
	mc.out_enable(mc_enable);
	mc.out_write(mc_write);
	mc.out_mask_w(mc_mask_w);
	mc.out_cycle(mc_cycle);

	for (i = 0; i < 4; i++) {
		mc.in_data[IF_RF][i](simdcluster_dram_data[IF_RF][i]);
		mc.in_data[IF_SP_WG0][i](simdcluster_dram_data[IF_SP_WG0][i]);
		mc.in_data[IF_SP_WG1][i](simdcluster_dram_data[IF_SP_WG1][i]);
		mc.out_vreg_idx_w[i](mc_vreg_idx_w[i]);
		mc.out_data[i](mc_out_data[i]);
	}

	simdcluster.iexecute_pipeline_stages(iexec_pipe_length);
	mc.set_refresh_counter(refc);
}

void
do_sim(void)
{
	compute_stats s;
	cmdarb_stats mcs;

	/* Run */
	sc_set_stop_mode(SC_STOP_FINISH_DELTA);

	if (ns)
		sc_start(ns, SC_NS);
	else
		sc_start();

	workscheduler.get_stats(s);
	simdcluster.get_stats(s);

	cout << endl;
	cout << s;

	mc.get_cmdarb_stats(mcs, (s.exec_time * mc.get_freq_MHz()) / 1000);
	if (debug_output[DEBUG_CMD_STATS]) {
		cout << endl;
		mcs.base_addr = 0;
		cout << mcs << endl;
	}
}

/** Document the parameters accepted by this binary.
 * @param Program name binary name used to invoke this program. */
void
help(char *program_name)
{
	unsigned int i;
	string::size_type j;

	cout << program_name << " [options] program.sas" << endl;
	cout << "Simulate execution of a Sim-D kernel." << endl;
	cout << endl;
	cout << "Options:" << endl;
	cout << "  -d [x,y]\t\t     : (x,y)-dimensions of program execution." << endl;
	cout << "  -w [t]\t\t     : Workgroup width, t a power-of-two > 32." << endl;
	cout << "  -n [ns]\t\t     : Simulation time in ns (default: 400)." << endl;
	cout << "  -P [stages]\t\t     : Number of execute pipeline stages (default: 1)." << endl;
	cout << "  -3\t\t\t     : Enable three-stage IDecode phase." << endl;
	cout << "  -i [buf,in.csv]\t     : Prior to execution, upload given file (CSV or" << endl;
	cout << "  \t\t\t       binary) into buffer indexed by [buf]." << endl;
	cout << "  -o [buf,out.txt]\t     : After execution, dump contents of given buffer" << endl;
	cout << "  \t\t\t       into file." << endl;
	cout << "  -c [buf,in.bin]\t     : After execution, compare the contents of given" << endl;
	cout << "  \t\t\t       buffer against the contents of the (binary) file" << endl;
	cout << "  \t\t\t       provided." << endl;
	cout << "  -e [error]\t\t     : Tolerable comparison error (delta or" << endl;
	cout << "  \t\t\t       percentage, default: 0.001)." << endl;
	cout << "  -b [width]\t\t     : Width (# 32-bit words) of a VRF SRAM bank." << endl;
	cout << "  -r [value]\t\t     : Initialise the memory controller's refresh counter." << endl;
	cout << "  -s schedopt[,schedopt[,..]]: Enable real-time scheduling options." << endl;
	cout << "  -D dbgopt[,dbgopt[,..]]    : Enable debugging output options." << endl;

	cout << endl;
	cout << "Scheduling options (schedopt):" << endl;

	for (i = 0; i < WSS_SENTINEL; i++) {
		cout << "  " << wss_opts[i].first;

		for (j = wss_opts[i].first.size(); j < 24; j++)
			cout << " ";

		cout << ": " <<	wss_opts[i].second << endl;
	}

	cout << endl;
	cout << "Debugging options (dbgopt):" << endl;

	for (i = 0; i < DEBUG_SENTINEL; i++) {
		cout << "  " << debug_output_opts[i].first;

		for (j = debug_output_opts[i].first.size(); j < 24; j++)
			cout << " ";

		cout << ": " <<	debug_output_opts[i].second << endl;
	}
}

void
print_program(void)
{
	prg.print_buffers();
	cout << endl;
	prg.print_sp_buffers();
	cout << endl;
	prg.print_branch_targets();
	cout << endl;
	prg.print_reg_usage();
	cout << endl;
	prg.print();
	cout << endl;
}

buffer_input_type
getBufferTypeFromFilename(string &file)
{
	size_t s;
	string extension;

	s = file.find_last_of('.');

	if (s == string::npos || s == file.length() - 1)
		return BINARY;

	extension = file.substr(s + 1);

	/** Find the extension */
	if (extension == "csv" || extension == "txt")
		return DECIMAL_CSV;

	return BINARY;
}

/** Parse command line parameters
 * @param argc Number of parameters given
 * @param argv List of strings, each containing one parameter
 * @param test The unit-test SystemC object connected to the top level of the
 * 	       memory controller
 */
void
parse_parameters(int argc, char* argv[])
{
	int c;
	int i;
	int wg_width;
	unsigned int pos;
	string::size_type sz;
	string oa;
	string dbgopt;
	unsigned int bufno;
	bool dims_provided = false;
	buffer_input_type t;
	string path;

	if (argc <= 1) {
		cout << "Missing program" << endl << endl;
		help(argv[0]);
		exit(1);
	}

	program = string(argv[argc-1]);

	ws_sched[WSS_STOP_SIM_FINI] = Log_1;

	/* Take stride patterns from the command line */
	while ( (c = getopt(argc - 1, argv, "hd:w:n:P:3i:o:c:e:b:s:D:r:")) != -1) {
		switch (c) {
		case 'h':
			help(argv[0]);
			exit(1);
			break;
		case 'd':
			oa = string(optarg);
			dims[0] = stoul(oa, &sz);
			oa = oa.substr(sz);
			if (oa.size() == 0) {
				dims[1] = 1;
				break;
			} else if (oa[0] != ',') {
				cout << "Error: Invalid dimension specification"
						<< endl << endl;
				help(argv[0]);
				exit(1);
			}

			oa = oa.substr(1);
			dims[1] = stoul(oa, &sz);
			dims_provided = true;
			break;
		case 'w':
			i = sscanf(optarg, "%i", &wg_width);
			if (i < 0 || wg_width < 32) {
				cout << "Error: Invalid number of simulation ns"
						<< endl << endl;
				help(argv[0]);
				exit(1);
			}

			wg_width = const_log2(wg_width >> 5);
			test.set_workgroup_width(workgroup_width(min(int(wg_width),int(WG_WIDTH_SENTINEL))));

			break;
		case 'n':
			i = sscanf(optarg, "%i", &ns);
			if (i == 0) {
				cout << "Error: Invalid number of simulation ns"
						<< endl << endl;
				help(argv[0]);
				exit(1);
			}
			break;
		case 'P':
			i = sscanf(optarg, "%i", &bufno);
			if (i == 0 || bufno == 32) {
				cout << "Error: Invalid number of pipeline stages"
						<< endl << endl;
				help(argv[0]);
				exit(1);
			}
			iexec_pipe_length = bufno;
			break;
		case '3':
			idec_impl = IDECODE_3S;
			break;
		case 'i':
			i = sscanf(optarg, "%i,%n", &bufno, &pos);
			if (i == 0 || bufno >= 32) {
				cout << "Error: Invalid buffer index" <<
						endl << endl;
				help(argv[0]);
				exit(1);
			}

			path = string(&optarg[pos]);
			t = getBufferTypeFromFilename(path);

			u.push_back({path, bufno, t});
			break;
		case 'o':
			i = sscanf(optarg, "%i,%n", &bufno, &pos);
			if (i == 0 || bufno >= 32) {
				cout << "Error: Invalid buffer index" <<
						endl << endl;
				help(argv[0]);
				exit(1);
			}

			path = string(&optarg[pos]);
			t = getBufferTypeFromFilename(path);

			d.push_back({download::ACTION_DOWNLOAD, path,
				bufno, t});
			break;
		case 'c':
			i = sscanf(optarg, "%i,%n", &bufno, &pos);
			if (i == 0 || bufno >= 32) {
				cout << "Error: Invalid buffer index" <<
						endl << endl;
				help(argv[0]);
				exit(1);
			}

			path = string(&optarg[pos]);
			t = getBufferTypeFromFilename(path);

			d.push_back({download::ACTION_COMPARE, path, bufno, t});
			break;
		case 'e':
			dfrac = (optarg[strlen(optarg)-1] == '%');

			i = sscanf(optarg, "%f", &delta);
			if (i == 0) {
				cout << "Error: No delta provided" <<
						endl << endl;
				help(argv[0]);
				exit(1);
			}

			if (dfrac)
				delta *= 0.01f;
			break;
		case 'r':
			i = sscanf(optarg, "%lu", &refc);
			if (i < 0 || refc > 15000) {
				cout << "Error: Invalid reference counter "
						"initialisation value" << endl
						<< endl;
				help(argv[0]);
				exit(1);
			}
			break;
		case 'b':
			i = sscanf(optarg, "%i", &bufno);
			if (i == 0 || bufno == 32) {
				cout << "Error: Invalid vrf_bank width"
						<< endl << endl;
				help(argv[0]);
				exit(1);
			}
			simdcluster.regfile_set_vrf_bank_words(bufno);
			break;
		case 's':
			oa = string(optarg);

			while (read_id(oa, dbgopt)) {
				for (i = 0; i < WSS_SENTINEL; i++) {
					if (dbgopt == wss_opts[i].first) {
						ws_sched[i] = Log_1;
						break;
					}
				}

				if (i == WSS_SENTINEL) {
					cout << "Error: unknown scheduling option \"" <<
						dbgopt << "\"" << endl << endl;
					help(argv[0]);
					exit(1);
				}

				if (oa[0] == ',')
					oa = oa.substr(1);
			}

			break;
		case 'D':
			oa = string(optarg);

			while (read_id(oa, dbgopt)) {
				for (i = 0; i < DEBUG_SENTINEL; i++) {
					if (dbgopt == debug_output_opts[i].first) {
						debug_output[i] = 1;
						break;
					}
				}

				if (i == DEBUG_SENTINEL) {
					cout << "Error: unknown debug option \"" <<
						dbgopt << "\"" << endl << endl;
					help(argv[0]);
					exit(1);
				}

				if (oa[0] == ',')
					oa = oa.substr(1);
			}

			break;
		default:
			help(argv[0]);
			exit(1);
		}
	}

	if (!dims_provided) {
		cout << "Error: No kernel dimensions provided" << endl << endl;
		help(argv[0]);
		exit(1);
	}
}

/** \mainpage Sim-D
 *
 * \section intro_sec Introduction
 *
 * Sim-D is a SIMD simulation environment used for Real-Time architecture
 * research.
 *
 */
int
sc_main(int argc, char* argv[])
{
	const ProgramBuffer *b;
	fstream fs;

	debug_output_reset();
	parse_parameters(argc, argv);

	if (!debug_output_validate())
		exit(1);

	elaborate();

	fs = fstream(program);
	if (!fs) {
		cout << "Could not open program file " << program << endl;
		exit(1);
	}
	prg.parse(fs);
	prg.resolve_branch_targets();
	/* Analysis folds the last exit into the store operation. */
	ControlFlow(prg);

	for (upload &ul : u) {
		ProgramBuffer &buf = prg.getBuffer(ul.buffer);
		if (buf.hasDataInputFile())
			cout << "Warning: overwriting buffer data input file "
				"for buffer " << ul.buffer <<" with command-"
				"line parameter." << endl;

		buf.setDataInputFile(ul.path, ul.type);
	}

	for (b = prg.buffer_begin(); b < prg.buffer_end(); b++) {
		if (b->hasDataInputFile())
			mc.debug_upload_buffer(*b);
	}

	if (debug_output[DEBUG_PROGRAM])
		print_program();

	do_sim();

	for (download &dl : d) {
		ProgramBuffer &buf = prg.getBuffer(dl.buffer);

		switch (dl.action) {
		case download::ACTION_DOWNLOAD:
			if (dl.type == BINARY)
				mc.debug_download_buffer_bin(buf, dl.path);
			else
				mc.debug_download_buffer_csv(buf, dl.path);
			break;
		case download::ACTION_COMPARE:
			if (dl.type == BINARY)
				mc.debug_compare_buffer_bin(buf, dl.path, delta, dfrac);
			else
				mc.debug_compare_buffer_csv(buf, dl.path, delta, dfrac);
			break;
		default:
			cout << "Error: Unknown action." << endl;
			help(argv[0]);
			exit(1);
		}

	}

	return 0;
}

