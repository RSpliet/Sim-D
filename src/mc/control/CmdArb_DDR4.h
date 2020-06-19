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

#ifndef MC_CONTROL_CMDARB_DDR4_H
#define MC_CONTROL_CMDARB_DDR4_H

#include <systemc>
#include <tlm>
#include <vector>
#include <algorithm>
#include <limits>
#include <utility>

/* Ramulator includes */
#include <ramulator/DDR4.h>
#include <ramulator/DRAM.h>

#include <libdrampower/LibDRAMPower.h>
#include <xmlparser/MemSpecParser.h>

#include "mc/model/cmd_DDR.h"
#include "mc/model/DQ_reservation.h"
#include "mc/model/cmdarb_stats.h"
#include "util/debug_output.h"
#include "util/defaults.h"

using namespace std;
using namespace sc_core;
using namespace sc_dt;
using namespace ramulator;
using namespace mc_model;
using namespace tlm;

namespace mc_control {

const static int ref_addr[int(DDR4::Level::MAX)] {
	[int(DDR4::Level::Channel)] = 0,
	[int(DDR4::Level::Rank)] = 0,
	[int(DDR4::Level::BankGroup)] = -1,
	[int(DDR4::Level::Bank)] = -1,
	[int(DDR4::Level::Row)] = -1,
	[int(DDR4::Level::Column)] = -1
};

const static pair<const pair<const string, const string>,const string> xml_map[] = {
	{{"DDR4_1866M", "DDR4_8Gb_x16"},"JEDEC_8Gb_DDR4-1866_16bit_M.xml"},
	{{"DDR4_3200AA","DDR4_8Gb_x16"},"MICRON_8Gb_DDR4-3200_16bit_G.xml"},
	{{"DDR4_3200AA","DDR4_8Gb_x8"},"MICRON_8Gb_DDR4-3200_8bit_G.xml"},
};

static const string findXMLFile(const string speed, const string org)
{
	for (auto e : xml_map) {
		if (e.first.first == speed && e.first.second == org)
			return e.second;
	}

	return "";
}

/** Command arbiter / scheduler for DDR4 DRAM.
 *
 * This component dispatches the final commands to RAMulator. It has three
 * responsibilities
 * - Timing correctness
 * - Efficient and predictable command scheduling
 * - Refresh
 *
 * For prioritisation, the following rules apply:
 * - Read/write always has priority over act
 * - But use the ~75% available cmdbus space to perform
 *   activates as early as possible
 * - Drain a bank-pair of its reads/writes prior to
 *   processing the r/w of other banks
 * - Skip to the next available bank-pair as soon as an
 *   implicit or explicit precharge is received
 * - Round-robin through the banks.
 *
 * @todo The interfacing with ramulator rather than out-ports makes it non-
 * trivial to perform unit-tests. Add assert() statements and prints inside
 * this module for testing purposes.
 */
template <unsigned int BUS_WIDTH, unsigned int DRAM_BANKS, unsigned int THREADS>
class CmdArb_DDR4 : public sc_core::sc_module
{
public:
	/** DRAM clock, SDR */
	sc_in<bool> in_clk{"in_clk"};

	/** One FIFO per bank - CAS/Precharge commands
	 * Using TLM FIFOs because they support inspecting its elements without
	 * popping them off.
	 * @todo FIFO depth? */
	sc_port<tlm_fifo<cmd_DDR<BUS_WIDTH,THREADS> > > in_cmd_fifo[DRAM_BANKS];

	/** DQ reservation fifo */
	sc_fifo_out<DQ_reservation<BUS_WIDTH,DRAM_BANKS,THREADS> >
					out_dq_fifo{"out_dq_fifo"};

	/** True iff at least one refresh operation is pending */
	sc_inout<bool> out_ref_pending{"out_ref_pending"};

	/** True iff a big request is currently being enumerated by CmdGen */
	sc_in<bool> in_cmdgen_busy{"in_cmdgen_busy"};

	/** MC-wide cycle counter */
	sc_in<long> in_cycle{"in_cycle"};

	/** All banks precharged, indicates passing of Least Issue Delay */
	sc_inout<bool> out_allpre{"out_allpre"};

	/** True if currently refreshing. */
	sc_inout<bool> out_ref{"out_ref"};

	/** Which WG is finished.
	 * Using a FIFO to efficiently handle the clock-crossing domain. */
	sc_fifo_out<RequestTarget> out_done_dst{"out_done_dst"};

	/** Construct thread */
	SC_CTOR(CmdArb_DDR4) : ddr4_pwr(nullptr), memSpec(nullptr),
			ddr4(nullptr), dram(nullptr), refi_count(0),
			ref_enq(0), allpre_cycle(numeric_limits<long>::min()),
			ref_fini_cycle(numeric_limits<long>::min())
	{
		unsigned int i;

		SC_THREAD(thread_lt);
		sensitive << in_clk.pos();

		SC_THREAD(thread_status)
		sensitive << in_clk.pos();

		for (i = 0; i < DRAM_BANKS; i++) {
			cmd_valid[i] = 0;
		}
	}

	/** Destructor */
	~CmdArb_DDR4()
	{
		if (ddr4_pwr) {
			delete ddr4_pwr;
			ddr4_pwr = nullptr;
		}

		if (memSpec) {
			delete memSpec;
			memSpec = nullptr;
		}

		if (dram) {
			delete dram;
			dram = nullptr;
		}

		if (ddr4) {
			delete ddr4;
			ddr4 = nullptr;
		}
	}

	/** Aggregate statistics into s.
	 * @param s Reference to structure to contain run-time stats.
	 * @param cycles Number of DDR clock cycles passed to program execution
	 * 		 finished. */
	void
	get_stats(cmdarb_stats &s, unsigned long cycles = 0)
	{
		/* Shallow copy */
		s = stats;

		if (!cycles)
			cycles = s.lid;

		s.dq_util = ((double) s.bytes * 100) /
				((double) cycles * BUS_WIDTH);

		ddr4_pwr->calcEnergy();
		s.energy = ddr4_pwr->getEnergy().total_energy;
		s.power = ddr4_pwr->getPower().average_power;
	}

	/** Return the exact clock period for our RAM organisation.
	 * @return The clock period for the active DRAM organisation. */
	double
	get_clk_period(void)
	{
		ram_ctor();
		return ddr4->speed_entry.tCK;
	}

	/** Return the DRAM frequency in MHz.
	 * @return DRAM frequency, in MHz. */
	unsigned long
	get_freq_MHz(void)
	{
		ram_ctor();
		return ddr4->speed_entry.rate / 2;
	}

	/**
	 * Initialise the refresh counter.
	 *
	 * This provides an easy means to vary the refresh alignment at the
	 * users discretion.
	 * @param refc Initialisation value for the refresh counter. Must be
	 * smaller than the DRAM spec's RFC.
	 */
	void
	set_refresh_counter(unsigned long refc)
	{
		/* We cannot check for valid ranges yet, as we won't construct
		 * the DRAM object until thread_lt kicks off. Just take the
		 * value and assume the user knows what they're doing.
		 */
		refi_count = refc;
	}

private:
	/** Statistics for quantitative analysis */
	cmdarb_stats stats = {0,0,0,0,0,0};

	/** Reference to DRAMPower object, for power estimation. */
	libDRAMPower *ddr4_pwr;

	/** DRAMPower memory specification object. */
	Data::MemorySpecification *memSpec;

	/** Desired DDR4 specification */
	DDR4 *ddr4;

	/** Ramulator DRAM object */
	DRAM<DDR4> *dram;

	/** Banked first command of the incoming FIFOs */
	cmd_DDR<BUS_WIDTH,THREADS> cmd[DRAM_BANKS];

	/** True iff the cmd entry for the DRAM bank is valid and not
	 * completely issued */
	bool cmd_valid[DRAM_BANKS];

	/** Refresh cycle counter */
	long refi_count;

	/** Number of refreshes enqueued */
	unsigned int ref_enq;

	/** Cycle at which allpre is complete and must be issued. */
	long allpre_cycle;

	/** Refresh finish counter. */
	long ref_fini_cycle;

	/** Cached RequestTarget. */
	RequestTarget dst;

	/** Construct various RAM model objects.
	 *
	 * Cannot be called in the constructor, because the initialisation
	 * order of static class members is undefined. Practically, ramulator's
	 * org_map and speed_map are initialised after statically calling the
	 * CmdArb constructor. The result is that the DDR4() constructor will
	 * fail in the CmdArb_DDR4 constructor as the lookup tables are still
	 * empty.
	 */
	void
	ram_ctor(void)
	{
		if (ddr4 == nullptr) {
			ddr4 = new DDR4(MC_DRAM_ORG, MC_DRAM_SPEED);
			ddr4->set_channel_number(MC_DRAM_CHANS);
			ddr4->set_rank_number(1);
			dram = new DRAM<DDR4>(ddr4, DDR4::Level::Channel);
		}

		if (memSpec == nullptr) {
			const string path = Data::MemSpecParser::getDefaultXMLPath();
			const string file = findXMLFile(MC_DRAM_SPEED,MC_DRAM_ORG);
			if (file == "") {
				throw invalid_argument("Could not identify valid DRAMPower XML file "
						"for provided DRAM speed and organisation. Please "
						"add an entry to xml_map in src/mc/control/CmdArb_DDR4.h "
						"and rebuild Sim-D.");
			}
			memSpec = new Data::MemorySpecification(
				Data::MemSpecParser::getMemSpecFromXML(path+"/memspecs/"+file));
			ddr4_pwr = new libDRAMPower(*memSpec, 0);
		}
	}

	/** Helper to convert SystemC bitfields into ramulator struct.
	 * Note that we take the low bank bits as a bank group to simplify
	 * interleaving two banks from different groups. That'll help avoid
	 * paying the long latencies for intra-bankgroup commands in unit-stride
	 * and most non-unit-stride transfers.
	 * @param cmd Command to take parameters from.
	 * @param bank Bank to which this command is directed.
	 * @param addr Vector of address components to store the translated
	 * address into.
	 */
	void
	xlat_addr_ramulator(cmd_DDR<BUS_WIDTH,THREADS> cmd, unsigned int bank,
			vector<int> &addr)
	{
		unsigned int bankgroups, banks;

		addr.resize(6);

		bankgroups = ddr4->org_entry.count[int(DDR4::Level::BankGroup)];
		banks = ddr4->org_entry.count[int(DDR4::Level::Bank)];

		addr[int(DDR4::Level::Channel)] = 0;
		addr[int(DDR4::Level::Rank)] = 0;
		addr[int(DDR4::Level::BankGroup)] = bank & (bankgroups - 1);
		addr[int(DDR4::Level::Bank)] =
				(bank >> const_log2(bankgroups)) & (banks - 1);
		addr[int(DDR4::Level::Row)] = cmd.row;
		addr[int(DDR4::Level::Column)] = cmd.col;
	}

	/** Read the head of each fifo into a local register bank. */
	void
	fetch_fifo_heads()
	{
		unsigned int i;

		for (i = 0; i < DRAM_BANKS; i++) {
			if (!cmd_valid[i]) {
				if (!in_cmd_fifo[i]->used())
					continue;

				cmd[i] = in_cmd_fifo[i]->get();
				cmd_valid[i] = 1;
			}
		}
	}

	/** Return true iff all FIFO cached ``head'' items are empty/invalid
	 * @return true iff all head elements are empty or invalid.
	 */
	bool
	fifo_heads_empty()
	{
		unsigned int i;

		for (i = 0; i < DRAM_BANKS; i++) {
			if (cmd_valid[i] || in_cmd_fifo[i]->used())
				return false;
		}

		return true;
	}

	/** For each FIFO, determine the minimum distance to the next precharge
	 *
	 * If a precharge is found, the number of items between the current
	 * and the next precharge will be reported. If no precharge is found,
	 * the total number of entries in the FIFO is reported.
	 *
	 * This provides a metric for a heuristic that picks the next activate
	 * command based on the likelihood that we have many reads/writes. These
	 * CAS operations provide enough time to hide other activate commands
	 * in if required. We believe this heuristic can provably improve worst-
	 * case latencies for indexed transfers, without harming WCET
	 * of (non-)unit stride transfers.
	 * @param pre_distance Min. distance between head of FIFO and next
	 * precharge
	 */
	void
	precharge_distance(int *pre_distance)
	{
		int i, j;
		cmd_DDR<BUS_WIDTH,THREADS> item;
		bool res;

		for (i = 0; i < DRAM_BANKS; i++) {
			pre_distance[i] = in_cmd_fifo[i]->used() - 1;
			if (cmd_valid[i]) {
				pre_distance[i]++;

				if (cmd[i].pre_post)
					pre_distance[i] = 1;
			}

			for (j = 0; j < in_cmd_fifo[i]->used(); j++) {
				res = in_cmd_fifo[i]->nb_peek(item, j);
				if (res)
					break;

				if (item.pre_post) {
					pre_distance[i] = j + 2;
					break;
				} else if (item.pre_pre) {
					pre_distance[i] = j + 1;
					break;
				}
			}
		}
	}

	/** For each type of DRAM command, pick the best candidate cmd based on
	 * availability and ``round-robin'' scheduling priority of banks
	 * @param bank The bank active for the previous cycle
	 * @param ppre_bank The best candidate for pre-activate precharge, or -1
	 * 		   if no viable candidate found.
	 * @param act_bank The best candidate cmd for activate, or -1 if no
	 *                 viable candiate found.
	 * @param rw_bank  The best candidate cmd for CAS operations, or -1 if
	 *                 no viable candiate found.
	 * @param p_bank   The best candidate cmd for precharge operations, or
	 *                 -1 if no viable candiate found.
	 * @param last_rw Pointer to a boolean, used to flag that there is
	 * 		  exactly one read/write command on the heads of the
	 * 		  input FIFOs. */
	void
	cmd_best_candidates(unsigned int bank, int *ppre_bank, int *act_bank,
			int *rw_bank, int *p_bank, bool *last_rw)
	{
		unsigned int i;
		int int_bank; /* Intermediate result for previous bank */
		DDR4::Command rml_cmd;
		vector<int> addr;
		int pre_dist[DRAM_BANKS];
		int act_fifo_entries = -1;
		unsigned int rw_count = 0;

		*ppre_bank = -1;
		*act_bank = -1;
		*rw_bank = -1;
		*p_bank = -1;

		int_bank = DRAM_BANKS - bank;

		precharge_distance(pre_dist);

		for (i = 0; i < DRAM_BANKS; i++) {
			if (!cmd_valid[i])
				continue;

			if (cmd[i].read || cmd[i].write)
				rw_count++;

			xlat_addr_ramulator(cmd[i], i, addr);

			/* This if-statement is a bit opaque and awkwardly
			 * styled, but it tests that:
			 * 1) The command is of a type we're looking for
			 * 2) Either we haven't picked a best candidate for this
			 *    command-type, (||) or the bank with this command
			 *    is closest to the bank used for the previous
			 *    command (round-robin)
			 * 3) The command is schedulable in this cycle
			 *
			 * For 2) the construction with adding DRAM_BANKS
			 * modulo DRAM_BANKS ensures that we always get a
			 * positive distance from the bank in the previous
			 * cycle. Under the assumption of 2^n banks, this
			 * is handled more efficiently in a hw implementation
			 * by a concept similar to omitting overflow checking
			 * of unsigned subtract.
			 */
			if (cmd[i].pre_pre) {
				if ((*ppre_bank < 0 ||
				   ((i + int_bank) % DRAM_BANKS) <
				   ((*ppre_bank + int_bank) % DRAM_BANKS)) &&
				   dram->check(DDR4::Command::PRE, addr.data(),
						   in_cycle.read()))
					*ppre_bank = i;
			} else if (cmd[i].act) {
				if ((*act_bank < 0 ||
				   (pre_dist[i] > act_fifo_entries ||
				   ((i + int_bank) % DRAM_BANKS) <
				   ((*act_bank + int_bank) % DRAM_BANKS))) &&
				   dram->check(DDR4::Command::ACT, addr.data(),
						   in_cycle.read())) {
					*act_bank = i;
					act_fifo_entries = pre_dist[i];
				}

			} else if ((cmd[i].read || cmd[i].write)) {

				if(*rw_bank >= 0 &&
				   ((i + int_bank) % DRAM_BANKS) >=
				   ((*rw_bank + int_bank) % DRAM_BANKS))
					continue;

				/** @todo Prioritise based on bank bundles */
				if (cmd[i].read) {
					rml_cmd = cmd[i].pre_post ?
							DDR4::Command::RDA :
							DDR4::Command::RD;
				} else {
					rml_cmd = cmd[i].pre_post ?
							DDR4::Command::WRA :
							DDR4::Command::WR;
				}

				/*assert(dram->check_row_hit(rml_cmd,
						addr.data()));*/

				if (dram->check(rml_cmd, addr.data(), in_cycle.read()))
					*rw_bank = i;
			} else if (cmd[i].pre_post &&
				  (*p_bank < 0 ||
				  ((i + int_bank) % DRAM_BANKS) <
				  ((*p_bank + int_bank) % DRAM_BANKS)) &&
				  dram->check(DDR4::Command::PRE, addr.data(),
						  in_cycle.read())) {
				*p_bank = i;
			}
		}

		*last_rw = (rw_count == 1);
	}

	/** Debug: Short-hand (trace) debug print statement.
	 * @param type Type of command found, string representation
	 * @param bank Bank associated with this command.
	 * @param cmd Command to print. */
	void
	print_cmd(const char *type, int bank, cmd_DDR<BUS_WIDTH,THREADS> *cmd)
	{
		if (debug_output[DEBUG_CMD_EMIT]) {
			cout << "@" << in_cycle.read() << ": " << type;
			if (bank >= 0)
				cout << " B(" << bank << ") " << " Target[" << cmd->target << "] " << *cmd;

			cout << endl;
		}
	}

	/** Perform refresh.
	 * @return true iff a refresh operation was successfully scheduled */
	bool
	refresh(void)
	{
		if (dram->check(DDR4::Command::REF, ref_addr, in_cycle.read())) {
			dram->update(DDR4::Command::REF, ref_addr, in_cycle.read());
			ddr4_pwr->doCommand(Data::MemCommand::REF, 0, in_cycle.read());
			stats.ref_c++;
			ref_fini_cycle = dram->get_next(DDR4::Command::REF, ref_addr);
			return true;
		}

		return false;
	}

	/** Update the least-issue delay used to determine when a DRAM transfer
	 * is fully finished.
	 * @param d Request target of currently issued request. Will be set
	 * 	    as the final destination once all checks clear.
	 */
	void
	update_lid(RequestTarget d)
	{
		if (in_cmdgen_busy.read())
			return;

		stats.lid = max(dram->get_next(DDR4::Command::REF, ref_addr),
				stats.lid);

		/* Sometimes the allpre_cycle is updated the moment it timed
		 * out, sending completion events more than one. Avoid by only
		 * setting the allpre_cycle counter if all command FIFOs are
		 * empty. */
		if (!fifo_heads_empty())
			return;

		/* Minus two to allow StrideSeq/IdxIt to start filling the
		 * pipeline early */
		allpre_cycle = max(dram->get_next(DDR4::Command::REF, ref_addr) - 2,
					allpre_cycle);
		dst = d;
	}

	/** Main thread */
	void
	thread_lt(void)
	{
		unsigned int bank = 0;
		unsigned int i;

		int ppre_bank;
		int act_bank;
		int rw_bank;
		int p_bank;

		bool last_rw;

		if (ddr4 == nullptr)
			ram_ctor();

		vector<int> addr;
		DDR4::Command rml_cmd;
		Data::MemCommand::cmds drp_cmd;
		DQ_reservation<BUS_WIDTH,DRAM_BANKS,THREADS> res;

		out_ref_pending.write(0);

		while (1) {
			/* Ramulator APIs force us to write this sequentially
			 *
			 * The following rules apply:
			 * - Read/write always has priority over act
			 * - But use the ~75% available cmdbus space to perform
			 *   activates as early as possible
			 * - Drain a bank-pair of its reads/writes prior to
			 *   processing the r/w of other banks
			 * - Skip to the next available bank-pair as soon as an
			 *   implicit or explicit precharge is received
			 * - Round-robin through the banks.
			 */

			/* Gather top-of-fifo commands */
			fetch_fifo_heads();

			/* Pick a candidate in each category */
			cmd_best_candidates(bank, &ppre_bank, &act_bank,
					&rw_bank, &p_bank, &last_rw);

			/* Issue, prioritised based on command type */
			if (rw_bank >= 0) {
				xlat_addr_ramulator(cmd[rw_bank], rw_bank,
						addr);
				if (cmd[rw_bank].read) {
					rml_cmd = cmd[rw_bank].pre_post ?
							DDR4::Command::RDA :
							DDR4::Command::RD;
					drp_cmd = cmd[rw_bank].pre_post ?
							Data::MemCommand::RDA :
							Data::MemCommand::RD;
				} else {
					rml_cmd = cmd[rw_bank].pre_post ?
							DDR4::Command::WRA :
							DDR4::Command::WR;
					drp_cmd = cmd[rw_bank].pre_post ?
							Data::MemCommand::WRA :
							Data::MemCommand::WR;
				}


				print_cmd("RW ",rw_bank, &cmd[rw_bank]);

				/* Mask off bit 0 to prioritise on bank pairs */
				bank = rw_bank & ~0x1;
				cmd_valid[rw_bank] = 0;
				dram->update(rml_cmd, addr.data(), in_cycle.read());
				ddr4_pwr->doCommand(drp_cmd, rw_bank, in_cycle.read());

				if (cmd[rw_bank].pre_post)
					update_lid(cmd[rw_bank].target);

				res.bank = rw_bank;
				res.col = cmd[rw_bank].col;
				res.row = cmd[rw_bank].row;
				res.wordmask = cmd[rw_bank].wordmask;
				res.write = cmd[rw_bank].write;
				res.sp_offset = cmd[rw_bank].sp_offset;
				res.cycle = in_cycle.read();
				res.target = cmd[rw_bank].target;
				for (i = 0; i < BUS_WIDTH; i++)
					res.reg_offset[i] = cmd[rw_bank].reg_offset[i];

				if (res.write) {
					/* -2 to account for scratchpad delay */
					res.cycle += ddr4->speed_entry.nCWL - 2;
					if (last_rw && !in_cmdgen_busy.read())
						stats.lda = res.cycle + 5;
				} else {
					res.cycle += ddr4->speed_entry.nCL;
					if (last_rw && !in_cmdgen_busy.read())
						stats.lda = res.cycle + 3;
				}
				out_dq_fifo.write(res);

				stats.cas_c++;
				for (i = 0; i < BUS_WIDTH; i++)
					if (res.wordmask[i])
						stats.bytes += 4;
			} else if (ppre_bank >= 0) {
				/* These come late and combined with act, better
				 * prioritise them over act */
				xlat_addr_ramulator(cmd[ppre_bank], ppre_bank,
						addr);
				rml_cmd = DDR4::Command::PRE;

				print_cmd("PRE", ppre_bank, &cmd[ppre_bank]);

				cmd[ppre_bank].pre_pre = 0;
						/* Keep for ACT/CAS/pre_post */
				dram->update(rml_cmd, addr.data(), in_cycle.read());
				ddr4_pwr->doCommand(Data::MemCommand::PRE,
						ppre_bank, in_cycle.read());
				stats.pre_c++;
				update_lid(cmd[ppre_bank].target);
			} else if (act_bank >= 0) {
				xlat_addr_ramulator(cmd[act_bank], act_bank,
						addr);
				rml_cmd = DDR4::Command::ACT;

				print_cmd("ACT", act_bank, &cmd[act_bank]);

				cmd[act_bank].act = 0; /* Keep for CAS/pre */
				dram->update(rml_cmd, addr.data(), in_cycle.read());
				ddr4_pwr->doCommand(Data::MemCommand::ACT,
						act_bank, in_cycle.read());
				stats.act_c++;
			} else if (p_bank >= 0) {
				xlat_addr_ramulator(cmd[p_bank], p_bank, addr);
				rml_cmd = DDR4::Command::PRE;

				print_cmd("PRE", p_bank, &cmd[p_bank]);

				cmd_valid[p_bank] = 0;
				dram->update(rml_cmd, addr.data(), in_cycle.read());
				ddr4_pwr->doCommand(Data::MemCommand::PRE,
						p_bank, in_cycle.read());
				stats.pre_c++;
				update_lid(cmd[p_bank].target);
			} else if (ref_enq && fifo_heads_empty() &&
					!in_cmdgen_busy.read()){
				/* I can schedule a refresh */
				if (refresh()) {
					print_cmd("REF", -1, nullptr);
					ref_enq--;
				}
			}

			/* Update refresh counter and enqueue refresh */
			refi_count++;
			if (refi_count >= ddr4->speed_entry.nREFI) {
				refi_count %= ddr4->speed_entry.nREFI;
				ref_enq++;
				assert(ref_enq <= 8); /* Per DDR4 specs */
			}

			out_ref_pending.write(ref_enq > 0);

			wait();
		}
	}

	/** Thread updating status bits. */
	void
	thread_status(void)
	{
		while (true) {
			if (in_cycle.read() == allpre_cycle) {
				out_allpre.write(true);
				out_done_dst.write(dst);
			} else {
				out_allpre.write(false);
			}

			out_ref.write(in_cycle.read() < ref_fini_cycle);

			wait();
		}
	}
};

}

#endif /* MC_CONTROL_CMDARB_DDR4 */
