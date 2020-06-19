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

#ifndef UTIL_DEFAULTS_H
#define UTIL_DEFAULTS_H

#include "util/constmath.h"

/* All of these should be overriden in cmake, hence the ifndef guards.
 * Safeguards added mainly to keep Eclipse happy in the absence of knowledge
 * about these symbols. */

/* Memory controller definitions */
#ifndef MC_DRAM_CHANS
#define MC_DRAM_CHANS 1
#endif

#ifndef MC_BIND_BUFS
#define MC_BIND_BUFS 32
#endif

#ifndef MC_DRAM_ORG
#define MC_DRAM_ORG "DDR4_8Gb_x16"
#endif

#ifndef MC_DRAM_SPEED
#define MC_DRAM_SPEED "DDR4_3200AA"
#endif

/* Unfortunately we can't pick the bank,row,col parameters from the DDR4 object,
 * because the templated objects we need for SystemC simulation require these
 * parameters to be constant. Manual labour for reconfiguration instead... */
#ifndef MC_DRAM_BANKS
#define MC_DRAM_BANKS 8
#elif (MC_DRAM_BANKS & (MC_DRAM_BANKS - 1)) != 0
#error "Configuration error: MC_DRAM_BANKS must be power of two."
#endif

#ifndef MC_DRAM_ROWS
#define MC_DRAM_ROWS 65536
#elif (MC_DRAM_ROWS & (MC_DRAM_ROWS - 1)) != 0
#error "Configuration error: MC_DRAM_ROWS must be power of two."
#endif

#ifndef MC_DRAM_COLS
#define MC_DRAM_COLS 1024
#elif (MC_DRAM_COLS & (MC_DRAM_COLS - 1)) != 0
#error "Configuration error: MC_DRAM_COLS must be power of two."
#endif

#ifndef MC_BURSTREQ_FIFO_DEPTH
#define MC_BURSTREQ_FIFO_DEPTH 16
#endif

/* This looks a bit redundant, being forced to 16, but is in preparation for
 * potential future work. */
#ifndef MC_BUS_WIDTH
#define MC_BUS_WIDTH 16
#elif MC_BUS_WIDTH != 16
#error "Configuration error: MC_BUS_WIDTH must be equal to 16. This " \
	"restriction might be lifted in the future."
#endif

/* Scratchpad definitions */
#ifndef SP_BYTES
#define SP_BYTES 131072
#elif (SP_BYTES & (SP_BYTES - 1)) != 0
#error "Configuration error: SP_BYTES must be power of two."
#endif

#ifndef SP_BUS_WIDTH
#define SP_BUS_WIDTH 4
#elif (SP_BUS_WIDTH & (SP_BUS_WIDTH - 1)) != 0
#error "Configuration error: SP_BUS_WIDTH must be power of two."
#elif SP_BUS_WIDTH < (MC_BUS_WIDTH/4)
#error "Configuration error: SP_BUS_WIDTH must be larger or equal to MC_BUS_WIDTH/4."
#endif

/* Compute definitions. */
#ifndef COMPUTE_THREADS
#define COMPUTE_THREADS 1024
#elif (COMPUTE_THREADS & (COMPUTE_THREADS - 1)) != 0
#error "Configuration error: COMPUTE_THREADS must be power of two."
#endif

#ifndef COMPUTE_FPUS
#define COMPUTE_FPUS 128
#elif (COMPUTE_FPUS & (COMPUTE_FPUS - 1)) != 0
#error "Configuration error: COMPUTE_FPUS must be power of two."
#endif

#ifndef COMPUTE_RCPUS
#define COMPUTE_RCPUS 32
#elif (COMPUTE_RCPUS & (COMPUTE_RCPUS - 1)) != 0
#error "Configuration error: COMPUTE_RCPUS must be power of two."
#endif

#ifndef COMPUTE_IMEM_INSNS
#define COMPUTE_IMEM_INSNS 2048
#elif (COMPUTE_IMEM_INSNS & (COMPUTE_IMEM_INSNS - 1)) != 0
#error "Configuration error: COMPUTE_IMEM_INSNS must be power of two."
#endif

#ifndef COMPUTE_CSTACK_ENTRIES
#define COMPUTE_CSTACK_ENTRIES 16
#endif

#define COMPUTE_PC_WIDTH const_log2(COMPUTE_IMEM_INSNS)

#endif /* UTIL_DEFAULTS_H */
