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

#ifndef SP_CONTROL_DQ_H
#define SP_CONTROL_DQ_H

#include <systemc>
#include "sp/model/DQ_reservation.h"

using namespace sc_core;
using namespace sc_dt;
using namespace sp_model;

namespace sp_control {

/** The scratchpad data path scheduler.
 *
 * Is responsible for synching the storage array up with the register file I/O
 * signals.
 * For reads (SP -> RF), SP data has to be requested directly. Stride sequencing
 * is long enough to take a cycle on itself. SA data will thus be available two
 * cycles later, meaning the (synchronous) RF signals must be held for one cycle.
 * For writes (RF -> SP), data must be requested from RF directly. Data for SA
 * arrives two cycles later.
 */
template <unsigned int BUS_WIDTH, unsigned int THREADS>
class DQ : public sc_module
{
private:
	/** Pipeline for StorageArray signals, delay for write operations. */
	dq_pipe_sa<BUS_WIDTH> pipe_sa;

	/** Pipeline for register file signals, delay for read operations. */
	dq_pipe_rf<BUS_WIDTH,THREADS> pipe_rf;

public:
	/** DRAM clock, SDR. */
	sc_in<bool> in_clk{"in_clk"};

	/** True if a read-operation (SP->RF) is taking place. */
	sc_in<bool> in_read{"in_read"};

	/** Stride sequencer output. */
	sc_fifo_in<sp_model::DQ_reservation<BUS_WIDTH,THREADS> >
			in_dq_fifo{"in_dq_fifo"};

	/******************** Register file interface *****************/
	/** Operation targets RF (enable bit). */
	sc_inout<bool> out_rf_rw{"in_rf_rw"};

	/** Write mask. */
	sc_inout<sc_bv<BUS_WIDTH> > out_rf_mask_w{"out_rf_mask_w"};

	/** Indexes for each incoming data word */
	sc_inout<reg_offset_t<THREADS> > out_rf_idx_w[BUS_WIDTH];

	/** Signal completion of a DQ reservation with the "last" bool set. */
	sc_inout<bool> out_done{"out_done"};

	/******************** StorageArray interface *****************/
	/** StorageArray RF signal bundle. */
	sc_inout<dq_pipe_sa<BUS_WIDTH> > out_sa_cmd{"out_sa_cmd"};

	/** Constructor */
	SC_CTOR(DQ)
	{
		SC_THREAD(thread);
		sensitive << in_clk.pos();
	}

	/** Commit register file signals to outputs. */
	void
	rf_commit(dq_pipe_rf<BUS_WIDTH,THREADS> &prf)
	{
		unsigned int i;

		out_rf_mask_w.write(prf.mask_w);
		out_rf_rw.write(prf.rw);
		for (i = 0; i < BUS_WIDTH; i++)
			out_rf_idx_w[i].write(prf.idx_w[i]);
	}

private:
	/** DQ scheduling thread. */
	void
	thread(void)
	{
		sp_model::DQ_reservation<BUS_WIDTH,THREADS> res;
		dq_pipe_rf<BUS_WIDTH,THREADS> prf;
		dq_pipe_sa<BUS_WIDTH> psa;

		while (true) {
			wait();
			wait(SC_ZERO_TIME);

			if (in_dq_fifo.num_available())
				res = in_dq_fifo.read();
			else
				res = sp_model::DQ_reservation<BUS_WIDTH,THREADS>();

			if (in_read.read()) {
				/* For reads (SP -> RF), SP data has to be
				 * requested directly. Stride sequencing is long
				 * enough to take a cycle on itself. SA data
				 * will thus be available two cycles later,
				 * meaning the (synchronous) RF signals must be
				 * held for one cycle. */
				prf = pipe_rf;
				pipe_rf = res.get_pipe_rf();
				psa = res.get_pipe_sa();

				out_done.write(prf.last);
			} else {
				/* Data must be requested from RF directly. Data
				 * for SA arrives two cycles later. */
				psa = pipe_sa;
				pipe_sa = res.get_pipe_sa();
				prf = res.get_pipe_rf();

				out_done.write(psa.last);
			}

			rf_commit(prf);
			out_sa_cmd.write(psa);
			wait(SC_ZERO_TIME);
		}
	}
};

}

#endif /* SP_CONTROL_DQ_H */
