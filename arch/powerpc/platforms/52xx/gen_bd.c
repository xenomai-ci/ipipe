/*
 * Driver for MPC52xx processor BestComm General Buffer Descriptor
 *
 * Copyright (C) 2006 AppSpec Computer Technologies Corp.
 *                    Jeff Gibbons <jeff.gibbons@appspec.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 *
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <asm/errno.h>
#include <asm/io.h>

#include <asm/mpc52xx.h>

#include "bestcomm.h"
#include "gen_bd.h"

/*
 * Initialize general buffer descriptor receive task.
 * We support up to four receive tasks.
 * Returns task number of general buffer descriptor receive task.
 * Returns -1 on failure
 */
int sdma_gen_bd_rx_init(int index, struct sdma *s, phys_addr_t fifo, 
                        int initiator, int ipr, int maxbufsize)
{
	struct sdma_gen_bd_rx_var *var;
	struct sdma_gen_bd_rx_inc *inc;

	static int tasknum[4] = { [ 0 ... 3] = -1 };
	static struct sdma_bd *bd[4] = { [ 0 ... 3] = 0 };
	static u32 bd_pa[4];

	if((index<0) || (index>4))
		return -1;

	if (tasknum[index] < 0) {
		tasknum[index] = sdma_load_task(sdma_gen_bd_rx_task);
		if (tasknum[index] < 0)
			return tasknum[index];
	}

	if (!bd[index])
		bd[index] = (struct sdma_bd *)
		            sdma_sram_alloc(sizeof(struct sdma_bd) * s->num_bd,
		                            SDMA_BD_ALIGN, &bd_pa[index]);
	if (!bd[index])
		return -ENOMEM;

	sdma_disable_task(tasknum[index]);

	s->tasknum = tasknum[index];
	s->bd = bd[index];
	s->flags = SDMA_FLAGS_NONE;
	s->index = 0;
	s->outdex = 0;
	memset(bd[index], 0, sizeof(struct sdma_bd) * s->num_bd);

	var = (struct sdma_gen_bd_rx_var *)sdma_task_var(tasknum[index]);
	var->enable = sdma_io_pa(&sdma.io->tcr[tasknum[index]]);
	var->fifo = fifo;
	var->bd_base = bd_pa[index];
	var->bd_last = bd_pa[index] + (s->num_bd - 1)*sizeof(struct sdma_bd);
	var->bd_start = bd_pa[index];
	var->buffer_size = maxbufsize;

	/* These are constants, they should have been in the image file */
	inc = (struct sdma_gen_bd_rx_inc *)sdma_task_inc(tasknum[index]);
	/* assume task_size0/1 of sdma.io = word(32 bytes) */
	inc->incr_bytes = -(s16)sizeof(u32);
	inc->incr_dst = sizeof(u32);

	sdma_set_task_pragma(tasknum[index], SDMA_GEN_RX_BD_PRAGMA);
	sdma_set_task_auto_start(tasknum[index], tasknum[index]);

	/* clear pending interrupt bits */
	out_be32(&sdma.io->IntPend, 1<<tasknum[index]);

	out_8(&sdma.io->ipr[initiator], ipr);
	sdma_set_initiator(tasknum[index], initiator);

	return tasknum[index];
}

/*
 * Initialize general buffer descriptor transmit task.
 * We support up to four transmit tasks.
 * Returns task number of general buffer descriptor transmit task.
 * Returns -1 on failure
 */
int sdma_gen_bd_tx_init(int index, struct sdma *s, phys_addr_t fifo, int initiator, int ipr)
{
	struct sdma_gen_bd_tx_var *var;
	struct sdma_gen_bd_tx_inc *inc;

	static int tasknum[4] = { [0 ... 3] = -1 };
	static struct sdma_bd *bd[4] = { [0 ... 3] = 0 };
	static u32 bd_pa[4];

	if (tasknum[index] < 0) {
		tasknum[index] = sdma_load_task(sdma_gen_bd_tx_task);
		if (tasknum[index] < 0)
			return tasknum[index];
	}

	if (!bd[index])
		bd[index] = (struct sdma_bd *)
		            sdma_sram_alloc(sizeof(struct sdma_bd) * s->num_bd,
		                            SDMA_BD_ALIGN, &bd_pa[index]);
	if (!bd[index])
		return -ENOMEM;

	sdma_disable_task(tasknum[index]);

	s->tasknum = tasknum[index];
	s->bd = bd[index];
	s->flags = SDMA_FLAGS_NONE;
	s->index = 0;
	s->outdex = 0;
	memset(bd[index], 0, sizeof(struct sdma_bd) * s->num_bd);

	var = (struct sdma_gen_bd_tx_var *)sdma_task_var(tasknum[index]);
	var->fifo = fifo;
	var->enable = sdma_io_pa(&sdma.io->tcr[tasknum[index]]);
	var->bd_base = bd_pa[index];
	var->bd_last = bd_pa[index] + (s->num_bd - 1)*sizeof(struct sdma_bd);
	var->bd_start = bd_pa[index];

	/* These are constants, they should have been in the image file */
	inc = (struct sdma_gen_bd_tx_inc *)sdma_task_inc(tasknum[index]);
	/* assume task_size0/1 of sdma.io = word(32 bytes) */
	inc->incr_bytes = -(s16)sizeof(u32);
	inc->incr_src = sizeof(u32);
	inc->incr_src_ma = sizeof(u8);

	sdma_set_task_pragma(tasknum[index], SDMA_GEN_TX_BD_PRAGMA);
	sdma_set_task_auto_start(tasknum[index], tasknum[index]);

	/* clear pending interrupt bits */
	out_be32(&sdma.io->IntPend, 1<<tasknum[index]);

	out_8(&sdma.io->ipr[initiator], ipr);
	sdma_set_initiator(tasknum[index], initiator);

	return tasknum[index];
}

EXPORT_SYMBOL(sdma_gen_bd_rx_init);
EXPORT_SYMBOL(sdma_gen_bd_tx_init);
