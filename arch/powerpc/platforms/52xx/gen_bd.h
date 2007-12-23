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

#ifndef __BESTCOMM_GEN_BD_H__
#define __BESTCOMM_GEN_BD_H__

/* rx task vars that need to be set before enabling the task */
struct sdma_gen_bd_rx_var {
	u32 enable;		/* (u16*) address of task's control register */
	u32 fifo;		/* (u32*) address of gen_bd's fifo */
	u32 bd_base;		/* (struct sdma_bd*) beginning of ring buffer */
	u32 bd_last;		/* (struct sdma_bd*) end of ring buffer */
	u32 bd_start;		/* (struct sdma_bd*) current bd */
	u32 buffer_size;	/* size of receive buffer */
};

/* rx task incs that need to be set before enabling the task */
struct sdma_gen_bd_rx_inc {
	u16 pad0;
	s16 incr_bytes;
	u16 pad1;
	s16 incr_dst;
};

/* tx task vars that need to be set before enabling the task */
struct sdma_gen_bd_tx_var {
	u32 fifo;		/* (u32*) address of gen_bd's fifo */
	u32 enable;		/* (u16*) address of task's control register */
	u32 bd_base;		/* (struct sdma_bd*) beginning of ring buffer */
	u32 bd_last;		/* (struct sdma_bd*) end of ring buffer */
	u32 bd_start;		/* (struct sdma_bd*) current bd */
	u32 buffer_size;	/* set by uCode for each packet */
};

/* tx task incs that need to be set before enabling the task */
struct sdma_gen_bd_tx_inc {
	u16 pad0;
	s16 incr_bytes;
	u16 pad1;
	s16 incr_src;
	u16 pad2;
	s16 incr_src_ma;
};

extern int sdma_gen_bd_rx_init(int index, struct sdma *s, phys_addr_t fifo,
                               int initiator, int ipr, int maxbufsize);
extern int sdma_gen_bd_tx_init(int index, struct sdma *s, phys_addr_t fifo, int initiator, int ipr);

extern u32 sdma_gen_bd_rx_task[];
extern u32 sdma_gen_bd_tx_task[];


#endif  /* __BESTCOMM_GEN_BD_H__ */
