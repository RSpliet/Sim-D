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
#include <list>

#include <ramulator/DDR4.h>

#include "mc/control/Backend.h"
#include "mc/control/StrideSequencer.h"

#include "sp/control/Scratchpad.h"

#include "model/stride_descriptor.h"

#include "isa/model/Program.h"
#include "isa/model/ProgramPhaseList.h"

#include "isa/analysis/CycleSim.h"
#include "isa/analysis/DRAMSim.h"
#include "isa/analysis/ControlFlow.h"
#include "isa/analysis/TimingDAG.h"

#include "compute/control/IDecode.h"

#include "util/constmath.h"
#include "util/defaults.h"
#include "util/debug_output.h"
#include "util/ddr4_lid.h"

using namespace mc_model;
using namespace mc_control;
using namespace sp_control;
using namespace dram;
using namespace std;
using namespace tlm;
using namespace simd_model;
using namespace sc_core;

namespace mc_test {

/** Test driver for the full memory controller design */
class Test_mc : public sc_core::sc_module
{
public:
	/** DRAM clock, SDR */
	sc_in<bool> in_clk{"in_clk"};

	/** Memory controller is done processing request. */
	sc_fifo_in<RequestTarget> in_mc_done_dst{"in_mc_done_dst"};

	/** The next address as normally generated by the stride sequencer. */
	sc_fifo_out<bool> out_trigger{"out_trigger"};

	/** FIFO of descriptors
	 * @todo Depth? */
	sc_fifo_out<stride_descriptor> out_desc{"out_desc"};

	sc_inout<sc_bv<WSS_SENTINEL>> out_sched_opts{"out_sched_opts"};

	/** Ticket number for next stride_descriptor ready to pop off
	 * DRAM/SPs FIFOs. */
	sc_inout<sc_uint<4> > out_ticket_pop{"out_ticket_pop"};

	/** Construct test thread */
	SC_CTOR(Test_mc)
	{
		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();
	}

	/** Enqueue a stride descriptor on the in-fifo.
	 * @param desc stride_descriptor to enqueue.*/
	void
	enqueue_stride_desc(stride_descriptor *desc)
	{
		out_desc.write(*desc);
	}

private:
	/** Main thread */
	void
	thread_lt(void)
	{
		sc_bv<WSS_SENTINEL> sched_opts;

		sched_opts = 0;
		sched_opts[WSS_STOP_DRAM_FINI] = Log_1;

		out_trigger.write(1);
		out_sched_opts.write(sched_opts);
		out_ticket_pop.write(0);
		wait();

		while (1) {
			if (in_mc_done_dst.num_available())
				in_mc_done_dst.read();

			wait();
		}
	}
};

}

using namespace mc_control;
using namespace mc_test;
using namespace ramulator;
using namespace compute_control;
using namespace isa_model;
using namespace isa_analysis;

static unsigned int wg_width;
static string program = "";
static IDecode_impl idec_impl = IDECODE_1S;
static unsigned int iexec_pipe_length = 3;
static unsigned long dims[2];
static bool dims_provided = false;

static Program prg;

/* SystemC: Full system */
static sc_clock *clk;

static Test_mc test("test");
static mc_control::StrideSequencer<MC_BUS_WIDTH,COMPUTE_THREADS> sseq("sseq");
static Backend<MC_DRAM_BANKS,MC_DRAM_COLS,MC_DRAM_ROWS,MC_BUS_WIDTH,COMPUTE_THREADS> mc("mc");
/** XXX: We may have to grow this SP to something unreasonably large to deal
 * with WCET derivation of full buffer indexed transfers. */
static sp_control::Scratchpad<0,MC_BUS_WIDTH/4,SP_BUS_WIDTH,131072,1024> sp("sp");

/* StrideSequencer -> CmdGen */
static sc_fifo<stride_descriptor> desc_fifo;
static sc_fifo<burst_request<MC_BUS_WIDTH,COMPUTE_THREADS> > req_fifo(MC_BURSTREQ_FIFO_DEPTH);
static sc_signal<bool> dangle_strseq_done;
static sc_fifo<bool> strseq_trigger(2);
static sc_signal<RequestTarget> strseq_dst;
static sc_signal<sc_bv<WSS_SENTINEL> > test_sched_opts;
static sc_signal<sc_uint<4> > test_ticket_pop;

static sc_signal<bool> ref_pending;
static sc_fifo<RequestTarget> mc_done_dst;
static sc_signal<bool> mc_allpre;
static sc_signal<AbstractRegister> sseq_vreg_reg_w;
static sc_signal<bool> mc_enable;
static sc_signal<reg_offset_t<COMPUTE_THREADS> > mc_vreg_idx_w[MC_BUS_WIDTH/4];

static sc_signal<sc_uint<32> > mc_out_data[MC_BUS_WIDTH/4];
static sc_signal<bool> mc_write;
static sc_signal<sc_uint<18> > mc_sp_addr;
static sc_signal<sc_uint<2> > mc_sp_words;
static sc_signal<long> mc_cycle;
static sc_signal<bool> dangle_mc_ref;

static sc_signal<sc_bv<MC_BUS_WIDTH/4> > mc_mask_w;
static sc_signal<sc_bv<MC_BUS_WIDTH/4> > mc_imask_w;
static sc_signal<sc_uint<32> > sp_data_mc[SP_BUS_WIDTH];

static sc_signal<bool> dangle_sseq_idx_push_trigger;
static sc_fifo<idx_t<COMPUTE_THREADS> > dangle_sseq_idx;

static sc_fifo<stride_descriptor> dangle_sp_desc_fifo;
static sc_fifo<bool> dangle_sp_trigger(2);
static sc_fifo<sc_uint<1> > dangle_sp_wg_done;
static sc_signal<bool> dangle_sp_rf_enable;
static sc_signal<bool> dangle_sp_rf_write;
static sc_signal<AbstractRegister> dangle_sp_rf_reg;
static sc_signal<sc_bv<SP_BUS_WIDTH> > dangle_sp_rf_mask;
static sc_signal<sc_bv<SP_BUS_WIDTH> > dangle_in_sp_rf_mask;
static sc_signal<reg_offset_t<COMPUTE_THREADS> > dangle_sp_rf_idx[SP_BUS_WIDTH];
#if SP_BUS_WIDTH != (MC_BUS_WIDTH/4)
	sc_signal<sc_uint<32> > sp_out_data[SP_BUS_WIDTH-(MC_BUS_WIDTH/4)];
#endif
static sc_signal<sc_uint<32> > dangle_regfile_store_data[SP_BUS_WIDTH];

static sc_signal<sc_uint<32> > dangle_data_mc[2][SP_BUS_WIDTH];

typedef enum {
	WCET_BC = 0,
	WCET_SINGLE_BUFFER,
	WCET_SP_AS_ACCESS,
	WCET_SP_AS_EXECUTE,
	WCET_SENTINEL
} wcet_t;

class WCETStats {
public:
	unsigned long wcet;
	unsigned int program_phases;
};

/** Wire up the design. */
void
elaborate()
{
	int i;
	sc_uint<const_log2(MC_DRAM_BANKS)> bank;
	sc_uint<const_log2(MC_DRAM_ROWS)> row;
	sc_uint<const_log2(MC_DRAM_COLS)> col;

	clk = new sc_clock("clk", sc_time(mc.get_clk_period(), SC_NS));

	test.in_clk(*clk);
	test.in_mc_done_dst(mc_done_dst);
	test.out_desc(desc_fifo);
	test.out_trigger(strseq_trigger);
	test.out_sched_opts(test_sched_opts);
	test.out_ticket_pop(test_ticket_pop);

	sseq.in_clk(*clk);
	sseq.in_desc_fifo(desc_fifo);
	sseq.in_ref_pending(ref_pending);
	sseq.in_trigger(strseq_trigger);
	sseq.out_req_fifo(req_fifo);
	sseq.out_done(dangle_strseq_done);
	sseq.in_DQ_allpre(mc_allpre);
	sseq.out_dst(strseq_dst);
	sseq.out_dst_reg(sseq_vreg_reg_w);
	sseq.out_idx_push_trigger(dangle_sseq_idx_push_trigger);
	sseq.in_idx(dangle_sseq_idx);
	sseq.in_cycle(mc_cycle);
	sseq.in_sched_opts(test_sched_opts);
	sseq.in_ticket_pop(test_ticket_pop);

	mc.in_clk(*clk);
	mc.in_req_fifo(req_fifo);
	mc.out_ref_pending(ref_pending);
	mc.out_allpre(mc_allpre);
	mc.out_ref(dangle_mc_ref);
	mc.in_mask_w(mc_imask_w);
	mc.out_sp_addr(mc_sp_addr);
	mc.out_enable(mc_enable);
	mc.out_write(mc_write);
	mc.out_mask_w(mc_mask_w);
	mc.out_done_dst(mc_done_dst);
	mc.out_cycle(mc_cycle);
	for (unsigned int i = 0; i < MC_BUS_WIDTH/4; i++) {
		mc.out_data[i](mc_out_data[i]);
		mc.in_data[IF_SP_WG0][i](sp_data_mc[i]);
		mc.in_data[IF_SP_WG1][i](dangle_data_mc[0][i]);
		mc.in_data[IF_RF][i](dangle_data_mc[1][i]);
		mc.out_vreg_idx_w[i](mc_vreg_idx_w[i]);
	}

	sp.in_clk(*clk);
	sp.in_sched_opts(test_sched_opts);
	sp.in_ticket_pop(test_ticket_pop);
	sp.in_desc_fifo(dangle_sp_desc_fifo);
	sp.in_trigger(dangle_sp_trigger);
	sp.out_wg_done(dangle_sp_wg_done);
	sp.out_rf_enable(dangle_sp_rf_enable);
	sp.out_rf_write(dangle_sp_rf_write);
	sp.out_rf_reg(dangle_sp_rf_reg);
	sp.out_rf_mask(dangle_sp_rf_mask);
	sp.in_rf_mask(dangle_in_sp_rf_mask);
	sp.in_dram_enable(mc_enable);
	sp.in_dram_dst(strseq_dst);
	sp.in_dram_write(mc_write);
	sp.in_dram_addr(mc_sp_addr);
	sp.in_dram_mask(mc_mask_w);

	for (i = 0; i < MC_BUS_WIDTH/4; i++) {
		sp.out_data[i](sp_data_mc[i]);
		sp.in_dram_data[i](mc_out_data[i]);
	}

	for (i = 0; i < SP_BUS_WIDTH; i++) {
		sp.out_rf_idx[i](dangle_sp_rf_idx[i]);
		sp.in_rf_data[i](dangle_regfile_store_data[i]);
	}

#if SP_BUS_WIDTH != (MC_BUS_WIDTH/4)
	for (i = 0; i < SP_BUS_WIDTH - (MC_BUS_WIDTH/4); i++)
		sp.out_data[(MC_BUS_WIDTH/4)+i](sp_out_data[i]);
#endif
	sp.elaborate();
}

/** Execute simulation
 * @param descs List of descriptors to pre-load the fifo with
 */
void
do_sim(stride_descriptor *sd, cmdarb_stats *stats)
{
	cmdarb_stats s;

	/* Enqueue stride patterns */
	test.enqueue_stride_desc(sd);

	/* Pre-fill scratchpad and DRAM */
	/** @todo We should probably read these patterns from a file too... */
	sp.debug_upload_test_pattern(0, 1024);
	mc.debug_upload_test_pattern(0x140000, 1024);

	/* Run */
	sc_start();

	/* Print out chunks of memory touched by the (static) test */
	//dram_print_range(0x140000, 1536, &cmdgen, &dq);
	//sp.debug_print_range(0x0, 256);
	mc.get_cmdarb_stats(s);
	mc.aggregate_cmdarb_stats(stats, s);
	if (debug_output[DEBUG_CMD_STATS]) {
		cout << s << endl;
	}
}

/** Document the parameters accepted by this binary.
 * @param Program name binary name used to invoke this program. */
void
help(char *program_name)
{
	unsigned int i;
	string::size_type j;

	cout << program_name << "[options] program.sas" << endl;
	cout << "Perform WCET analysis on a Sim-D kernel." << endl;
	cout << endl;
	cout << "Options:" << endl;
	cout << "  -w [t]\t\t     : Workgroup width, t a power-of-two > 32." << endl;
	cout << "  -P [stages]\t\t     : Number of execute pipeline stages (default: 1)." << endl;
	cout << "  -3\t\t\t     : Enable three-stage IDecode phase." << endl;
	cout << "  -D dbgopt[,dbgopt[,..]]    : Enable debugging output options." << endl;

	cout << endl;
	cout << "Debugging options (dbgopt):" << endl;

	for (i = 0; i < DEBUG_SENTINEL; i++) {
		if (debug_output_opts[i].first.substr(0,4) == "pipe")
			continue;

		cout << "  " << debug_output_opts[i].first;

		for (j = debug_output_opts[i].first.size(); j < 24; j++)
			cout << " ";

		cout << ": " <<	debug_output_opts[i].second << endl;
	}
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
	string oa;
	string dbgopt;
	string::size_type sz;
	unsigned int bufno;

	program = string(argv[argc-1]);

	/* Take stride patterns from the command line */
	while ( (c = getopt(argc - 1, argv, "w:P:d:3D:")) != -1) {
		switch (c) {
		case 'w':
			i = sscanf(optarg, "%i", &wg_width);
			if (i < 0 || wg_width < 32) {
				cout << "Error: Invalid number of simulation ns"
						<< endl << endl;
				help(argv[0]);
				exit(1);
			}

			wg_width = const_log2(wg_width >> 5);
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
		case '3':
			idec_impl = IDECODE_3S;
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
		cout << "Error: no dimensions provided." << endl;
		help(argv[0]);
		exit(1);
	}
}

unsigned long
sim_DRAM_stride(stride_descriptor &sd, bool sweep)
{
	cmdarb_stats *stats;
	unsigned int i;
	int pid;
	unsigned long lda;
	unsigned int loop_bound;

	stats = mc.allocate_cmdarb_stats();

	if (sweep)
		loop_bound = MC_DRAM_COLS * 4;
	else
		loop_bound = 1;

	/* Tighter upper bound on i based on buffer properties. */
	for (i = 0; i < loop_bound; i++) {
		pid = fork();
		if (pid == 0) {
			do_sim(&sd, stats);
			exit(0);
		}

		waitpid(pid, NULL, 0);

		sd.addr += 4;
	}

	/* WCET based on least-issue delay. */
	lda = stats[STATS_MAX].lid;

	mc.free_cmdarb_stats(stats);

	return lda;
}

unsigned long
workgroups(void)
{
	unsigned long wgs;
	unsigned long wgw, wgh;

	wgw = (32ul << wg_width);
	wgh = COMPUTE_THREADS / wgw;

	wgs = (dims[0] + (wgw - 1)) / wgw;
	wgs *= (dims[1] + (wgh - 1)) / wgh;

	return wgs;
}

void
wcet(WCETStats &s, ProgramPhaseList *ppl, unsigned long prg_upload_cycles,
		const dram_timing *dram)
{
	s.wcet = ppl->WCET(workgroups()) + prg_upload_cycles;
	s.wcet = inflate_refresh(dram, s.wcet);
	s.program_phases = ppl->countPhases();
}

void
print_wcet_stats(WCETStats *s)
{
	unsigned int i;

	cout << "=== Results" << endl;

	cout << "\t\tBest-case\tSingle-buffered\tSP as DRAM\tSP as compute" << endl;
	cout << "WCET\t\t";
	for (i = 0; i < WCET_SENTINEL; i++)
		cout << std::setiosflags(ios::left) << setw(14) << s[i].wcet << "\t";
	cout << endl;

	cout << "Prog. phases\t";
	for (i = 0; i < WCET_SENTINEL; i++)
		cout << std::setiosflags(ios::left) << setw(14) << s[i].program_phases << "\t";

	cout << endl;
}

void
print_program_stats(Program &p)
{
	if (debug_output[DEBUG_PROGRAM]) {
		prg.print_loops();
		prg.print();
		prg.print_reg_usage();
		cout << "CSTACK entries          : " <<
			ControlFlow_CSTACKMaxDepth() <<	endl;
	}
}

/** Main SystemC thread.
 * @param argc Number of strings in argv.
 * @param argv Command-line parameters.
 * @return 0 iff program ended successfully.
 */
int
sc_main(int argc, char* argv[])
{
	fstream fs;
	unsigned long pipe_depth;
	unsigned long prg_upload_cycles;
	WCETStats s[WCET_SENTINEL];
	const dram_timing *dram;
	DAG *dag;
	DAG *critPath;
	ProgramPhaseList *ppl;
	unsigned long wcet_lb_pp;
	unsigned long wcet_lb_db;

	/* Suppress frequent "simulation stopped by user" messages */
	sc_report_handler::set_verbosity_level(SC_LOW);

	/* Now that test is set up correctly, we can start parsing params */
	debug_output_reset();
	parse_parameters(argc, argv);

	if (!debug_output_validate())
		exit(1);

	pipe_depth = iexec_pipe_length;
	switch (idec_impl) {
	case IDECODE_1S:
		pipe_depth += 1;
		break;
	case IDECODE_3S:
		pipe_depth += 3;
		break;
	default:
		cerr << "Cannot infer pipeline depth from implementation." << endl;
		exit(-1);
	}

	elaborate();
	dram = getTiming(MC_DRAM_SPEED, MC_DRAM_ORG, MC_DRAM_BANKS / 4);

	fs = fstream(program);
	if (!fs) {
		cout << "Could not open program file " << program << endl;
		help(argv[0]);
		exit(1);
	}
	prg.parse(fs, true);
	prg.resolve_branch_targets();

	if (debug_output[DEBUG_WCET_PROGRESS])
		cout << "=== Worst-case execution time analysis" << endl;

	/* Analysis passes. */
	ControlFlow(prg);
	DRAMSim(prg, workgroup_width(min(int(wg_width),int(WG_WIDTH_SENTINEL))),
			dram, sim_DRAM_stride);
	prg_upload_cycles = ProgramUploadTime(prg, dram);
	CycleSim(prg, idec_impl, iexec_pipe_length);
	dag = TimingDAG(prg);

	critPath = criticalPath(dag);
	ppl = new ProgramPhaseList(critPath, pipe_depth);
	wcet_lb_pp = ppl->PerfectParallelismWCETLB(dram, workgroups()) + prg_upload_cycles;
	wcet_lb_db = inflate_refresh(dram, ppl->DoubleBufferedWCETLB(workgroups())) + prg_upload_cycles;
	s[WCET_BC].wcet = max(wcet_lb_pp,wcet_lb_db);
	s[WCET_BC].program_phases = ppl->countPhases();

	s[WCET_SINGLE_BUFFER].wcet = ppl->SingleBufferedWCET(workgroups()) + prg_upload_cycles;
	s[WCET_SINGLE_BUFFER].wcet = inflate_refresh(dram, s[WCET_SINGLE_BUFFER].wcet);
	s[WCET_SINGLE_BUFFER].program_phases = ppl->countPhases();

	wcet(s[WCET_SP_AS_ACCESS], ppl, prg_upload_cycles, dram);

	critPath = criticalPath(dag, true);
	ppl = new ProgramPhaseList(critPath, pipe_depth);
	wcet(s[WCET_SP_AS_EXECUTE], ppl, prg_upload_cycles, dram);

	if (debug_output[DEBUG_WCET_PROGRESS])
		cout << endl;

	print_program_stats(prg);
	print_wcet_stats(s);

	return 0;
}
