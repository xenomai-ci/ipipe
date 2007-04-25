/*
 * Copyright(c) 2006 DENX Engineering. All rights reserved.
 *
 * Author: Yuri Tikhonov <yur@emcraft.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 */

/*
 *  This driver supports the asynchrounous DMA copy and RAID engines available
 * on the AMCC PPC440SPe Processors.
 *  Based on the Intel Xscale(R) family of I/O Processors (IOP 32x, 33x, 134x)
 * ADMA driver.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/async_tx.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <asm/ppc440spe_adma.h>

#define to_ppc440spe_adma_chan(chan) container_of(chan,ppc440spe_ch_t,common)
#define to_ppc440spe_adma_device(dev) container_of(dev,ppc440spe_dev_t,common)
#define tx_to_ppc440spe_adma_slot(tx) container_of(tx,ppc440spe_desc_t,async_tx)

#define PPC440SPE_ADMA_WATCHDOG_MSEC		3

#define PPC440SPE_ADMA_MAX_BYTE_COUNT		0xFFFFFF

#define PPC440SPE_ADMA_DEBUG 0
#define PRINTK(x...) ((void)(PPC440SPE_ADMA_DEBUG && printk(x)))

static void ppc440spe_chan_start_null_xor(ppc440spe_ch_t *chan);

/* This flag is set when want to refetch the xor chain in the interrupt handler */
static u32 do_xor_refetch = 0;

/* Pointers to last submitted to DMA0, DMA1 CDBs */
static ppc440spe_desc_t *chan_last_sub[2] = { NULL, NULL };

/* Pointer to last linked and submitted xor CB */
static ppc440spe_desc_t *xor_last_linked = NULL;
static ppc440spe_desc_t *xor_last_submit = NULL;

/******************************************************************************
 * Command (Descriptor) Blocks low-level routines
 ******************************************************************************/

/**
 * ppc440spe_desc_init_interrupt - initialize the descriptor for INTERRUPT pseudo operation
 */
static inline void ppc440spe_desc_init_interrupt (ppc440spe_desc_t *desc,
								ppc440spe_ch_t *chan)
{
	xor_cb_t *p;

	switch (chan->device->id) {
	case PPC440SPE_XOR_ID:
		p = desc->hw_desc;
		memset (desc->hw_desc, 0, sizeof(xor_cb_t));
		p->cbc = XOR_CBCR_CBCE_BIT; /* Command Block Complete Enable */
		break;
	default:
		printk("%s is not supported for chan %d\n",
			__FUNCTION__, chan->device->id);
		break;
	}
}

/**
 * ppc440spe_desc_init_null_xor - initialize the descriptor for NULL XOR pseudo operation
 */
static inline void ppc440spe_desc_init_null_xor(ppc440spe_desc_t *desc)
{
	xor_cb_t *hw_desc = desc->hw_desc;

	memset (desc->hw_desc, 0, sizeof(xor_cb_t));
	desc->hw_next = NULL;
	desc->src_cnt = 0;
}

/**
 * ppc440spe_desc_init_xor - initialize the descriptor for XOR operation
 */
static inline void ppc440spe_desc_init_xor(ppc440spe_desc_t *desc, int src_cnt, int int_en)
{
	xor_cb_t *hw_desc = desc->hw_desc;

	memset (desc->hw_desc, 0, sizeof(xor_cb_t));
	desc->hw_next = NULL;
	desc->src_cnt = src_cnt;

	hw_desc->cbc = XOR_CBCR_TGT_BIT | src_cnt;
	if (int_en)
		hw_desc->cbc |= XOR_CBCR_CBCE_BIT; /* Enable interrupt on complete */
}

/**
 * ppc440spe_desc_init_memcpy - initialize the descriptor for MEMCPY operation
 */
static inline void ppc440spe_desc_init_memcpy(ppc440spe_desc_t *desc, int int_en)
{
	dma_cdb_t *hw_desc = desc->hw_desc;

	memset (desc->hw_desc, 0, sizeof(dma_cdb_t));
	desc->hw_next = NULL;
	desc->src_cnt = 1;

	if (int_en)
		desc->flags |= PPC440SPE_DESC_INT;
	else
		desc->flags &= ~PPC440SPE_DESC_INT;

	hw_desc->opc = DMA_CDB_OPC_MV_SG1_SG2;
}

/**
 * ppc440spe_desc_set_src_addr - set source address into the descriptor
 */
static inline void ppc440spe_desc_set_src_addr( ppc440spe_desc_t *desc,
					ppc440spe_ch_t *chan, int src_idx, dma_addr_t addr)
{
	dma_cdb_t *dma_hw_desc;
	xor_cb_t *xor_hw_desc;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		dma_hw_desc = desc->hw_desc;
		dma_hw_desc->sg1l = cpu_to_le32(addr);
		break;
	case PPC440SPE_XOR_ID:
		xor_hw_desc = desc->hw_desc;
		xor_hw_desc->ops[src_idx] = addr;
		break;
	}
}

/**
 * ppc440spe_desc_set_dest_addr - set destination address into the descriptor
 */
static inline void ppc440spe_desc_set_dest_addr(ppc440spe_desc_t *desc,
							ppc440spe_ch_t *chan, dma_addr_t addr)
{
	dma_cdb_t *dma_hw_desc;
	xor_cb_t *xor_hw_desc;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		dma_hw_desc = desc->hw_desc;
		dma_hw_desc->sg2l = cpu_to_le32(addr);
		break;
	case PPC440SPE_XOR_ID:
		xor_hw_desc = desc->hw_desc;
		xor_hw_desc->cbtal = addr;
		break;
	}
}

/**
 * ppc440spe_desc_set_byte_count - set number of data bytes involved into the operation
 */
static inline void ppc440spe_desc_set_byte_count(ppc440spe_desc_t *desc,
						ppc440spe_ch_t *chan, u32 byte_count)
{
	dma_cdb_t *dma_hw_desc;
	xor_cb_t *xor_hw_desc;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		dma_hw_desc = desc->hw_desc;
		dma_hw_desc->cnt = cpu_to_le32(byte_count);
		break;
	case PPC440SPE_XOR_ID:
		xor_hw_desc = desc->hw_desc;
		xor_hw_desc->cbbc = byte_count;
		break;
	}
}

/**
 * ppc440spe_xor_set_link - set link address in xor CB
 */
static inline void ppc440spe_xor_set_link (ppc440spe_desc_t *prev_desc,
							ppc440spe_desc_t *next_desc) {
	xor_cb_t *xor_hw_desc = prev_desc->hw_desc;

	if (unlikely(!next_desc)) {
		printk("%s next is NULL\n", __FUNCTION__);
		BUG();
	}

	xor_hw_desc->cbs = 0;
	xor_hw_desc->cblal = next_desc->phys;
	xor_hw_desc->cbc |= XOR_CBCR_LNK_BIT;
}

/**
 * ppc440spe_desc_set_link - set the address of descriptor following this descriptor in chain
 */
static inline void ppc440spe_desc_set_link(ppc440spe_ch_t *chan,
				ppc440spe_desc_t *prev_desc, ppc440spe_desc_t *next_desc)
{
	unsigned long flags;

	if (unlikely(!prev_desc || !next_desc)) {
		printk("%s: set prev %p and next %p\n", __FUNCTION__, prev_desc, next_desc);
		BUG();
	}

	local_irq_save(flags);

	/* do s/w chaining both for DMA and XOR descriptors */
	prev_desc->hw_next = next_desc;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		break;
	case PPC440SPE_XOR_ID:
		/* bind descriptor to the chain */
		xor_last_linked = next_desc;

		if (prev_desc == xor_last_submit)
			/* do not link to the last submitted CB */
			break;
		ppc440spe_xor_set_link (prev_desc, next_desc);
		break;
	}

	local_irq_restore(flags);
}

/**
 * ppc440spe_desc_get_src_addr - extract the source address from the descriptor
 */
static inline u32 ppc440spe_desc_get_src_addr(ppc440spe_desc_t *desc,
						ppc440spe_ch_t *chan, int src_idx)
{
	dma_cdb_t *dma_hw_desc;
	xor_cb_t *xor_hw_desc;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		dma_hw_desc = desc->hw_desc;
		return le32_to_cpu(dma_hw_desc->sg1l);
	case PPC440SPE_XOR_ID:
		xor_hw_desc = desc->hw_desc;
		return xor_hw_desc->ops[src_idx];
	}
	return 0;
}

/**
 * ppc440spe_desc_get_dest_addr - extract the destination address from the descriptor
 */
static inline u32 ppc440spe_desc_get_dest_addr(ppc440spe_desc_t *desc, ppc440spe_ch_t *chan)
{
	dma_cdb_t *dma_hw_desc;
	xor_cb_t *xor_hw_desc;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		dma_hw_desc = desc->hw_desc;
		return le32_to_cpu(dma_hw_desc->sg2l);
	case PPC440SPE_XOR_ID:
		xor_hw_desc = desc->hw_desc;
		return xor_hw_desc->cbtal;
	}
	return 0;
}

/**
 * ppc440spe_desc_get_byte_count - extract the byte count from the descriptor
 */
static inline u32 ppc440spe_desc_get_byte_count(ppc440spe_desc_t *desc, ppc440spe_ch_t *chan)
{
	dma_cdb_t *dma_hw_desc;
	xor_cb_t *xor_hw_desc;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		dma_hw_desc = desc->hw_desc;
		return le32_to_cpu(dma_hw_desc->cnt);
	case PPC440SPE_XOR_ID:
		xor_hw_desc = desc->hw_desc;
		return xor_hw_desc->cbbc;
	}
	return 0;
}

/**
 * ppc440spe_desc_get_link - get the address of the descriptor that follows this one
 */
static inline u32 ppc440spe_desc_get_link(ppc440spe_desc_t *desc, ppc440spe_ch_t *chan)
{
	if (!desc->hw_next)
		return 0;

	return desc->hw_next->phys;
}

/**
 * ppc440spe_desc_is_aligned - check alignment
 */
static inline int ppc440spe_desc_is_aligned(ppc440spe_desc_t *desc, int num_slots)
{
	return (desc->idx & (num_slots - 1)) ? 0 : 1;
}

/**
 * ppc440spe_chan_xor_slot_count - get the number of slots necessary for XOR operation
 */
static inline int ppc440spe_chan_xor_slot_count(size_t len, int src_cnt, int *slots_per_op)
{
	int slot_cnt;

	/* each XOR descriptor provides up to 16 source operands */
	slot_cnt = *slots_per_op = (src_cnt + XOR_MAX_OPS - 1)/XOR_MAX_OPS;

	if (likely(len <= PPC440SPE_ADMA_XOR_MAX_BYTE_COUNT))
		return slot_cnt;

	printk("%s: len %d > max %d !!\n", __FUNCTION__, len,
		PPC440SPE_ADMA_XOR_MAX_BYTE_COUNT);
	BUG();
	return slot_cnt;
}

/******************************************************************************
 * ADMA channel low-level routines
 ******************************************************************************/

static inline u32 ppc440spe_chan_get_current_descriptor(ppc440spe_ch_t *chan);
static inline void ppc440spe_chan_append(ppc440spe_ch_t *chan);

/**
 * ppc440spe_adma_device_clear_eot_status - interrupt ack to XOR or DMA engine
 */
static inline void ppc440spe_adma_device_clear_eot_status (ppc440spe_ch_t *chan)
{
	volatile dma_regs_t *dma_reg;
	volatile xor_regs_t *xor_reg;
	u32 rv;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		/* read FIFO to ack */
		dma_reg = (dma_regs_t *)chan->device->pdev->resource[0].start;

		if (!dma_reg->csfpl)
			printk ("%s: CSFPL is NULL\n", __FUNCTION__);
		rv = dma_reg->dsts;
		if (rv)
			printk("DMA%d err status: 0x%x\n", chan->device->id,
				le32_to_cpu(rv));
		break;
	case PPC440SPE_XOR_ID:
		/* reset status bits to ack*/
		xor_reg = (xor_regs_t *)chan->device->pdev->resource[0].start;

		rv = xor_reg->sr;
		xor_reg->sr = rv;

                if (rv & (XOR_IE_ICBIE_BIT|XOR_IE_ICIE_BIT|XOR_IE_RPTIE_BIT)) {
			printk ("XOR ERR 0x%x status\n", rv);
			if (rv & XOR_IE_RPTIE_BIT) {
				/* Read PLB Timeout Error. 
				 * Try to resubmit the CB
				 */
				xor_reg->cblalr = xor_reg->ccbalr;
				xor_reg->crsr = XOR_CRSR_XAE_BIT;
			}
			break;
		}

		/*  if the XORcore is idle, but there are unprocessed CBs
		 * then refetch the s/w chain here
		 */
		if (!(xor_reg->sr & XOR_SR_XCP_BIT) && do_xor_refetch) {
			ppc440spe_chan_append(chan);
		}
		break;
	}
}

/**
 * ppc440spe_chan_idle - stop the watch-dog timer if channel is idle
 */
static inline void ppc440spe_chan_idle(int busy, ppc440spe_ch_t *chan)
{
	if (!busy)
		del_timer(&chan->cleanup_watchdog);
}

/**
 * ppc440spe_chan_is_busy - get the channel status
 */
static inline int ppc440spe_chan_is_busy(ppc440spe_ch_t *chan)
{
	int busy = 0;
	volatile xor_regs_t *xor_reg;
	volatile dma_regs_t *dma_reg;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		dma_reg = (dma_regs_t *)chan->device->pdev->resource[0].start;
		/*  if command FIFO's head and tail pointers are equal -
		 * channel is free
		 */
		busy = (dma_reg->cpfhp != dma_reg->cpftp) ? 1 : 0;
		break;
	case PPC440SPE_XOR_ID:
		/* use the special status bit for the XORcore
		 */
		xor_reg = (xor_regs_t *)chan->device->pdev->resource[0].start;
		busy = (xor_reg->sr & XOR_SR_XCP_BIT) ? 1 : 0;
		break;
	}

	return busy;
}

/**
 * ppc440spe_chan_set_first_xor_descriptor -  initi XORcore chain
 */
static inline void ppc440spe_chan_set_first_xor_descriptor(ppc440spe_ch_t *chan,
								ppc440spe_desc_t *next_desc)
{
	volatile xor_regs_t *xor_reg;

	xor_reg = (xor_regs_t *)chan->device->pdev->resource[0].start;

	if (xor_reg->sr & XOR_SR_XCP_BIT)
		printk("Warn: XORcore is running when try to set the first CDB!\n");

	xor_last_submit = xor_last_linked = next_desc;
	xor_reg->cblalr = next_desc->phys;
	xor_reg->cbcr |= XOR_CBCR_LNK_BIT;
}

/**
 * ppc440spe_chan_append - update the h/w chain in the channel
 */
static inline void ppc440spe_chan_append(ppc440spe_ch_t *chan)
{
	volatile dma_regs_t *dma_reg;
	volatile xor_regs_t *xor_reg;
	ppc440spe_desc_t *iter;
	u32 cur_desc;
	unsigned long flags;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		dma_reg = (dma_regs_t *)chan->device->pdev->resource[0].start;
		cur_desc = ppc440spe_chan_get_current_descriptor(chan);

		if (likely(cur_desc)) {
			/* flush descriptors from the s/w queue to fifo */
			iter = chan_last_sub[chan->device->id];
			if (!iter->hw_next)
				return;

			list_for_each_entry_continue(iter, &chan->chain, chain_node) {
				cur_desc = iter->phys;
				if (!list_empty(&iter->async_tx.depend_list)) {
					/*  if there are descriptors dependent on this
					 * desc, then force it to generate interrupt on
					 * complete
					 */
					iter->flags |= PPC440SPE_DESC_INT;
				}

				/* set "generate interrupt" bit if necessary */
				if(!(iter->flags & PPC440SPE_DESC_INT))
					cur_desc |= DMA_CDB_NO_INT;

				/* put descriptor into FIFO */
				out_le32 (&dma_reg->cpfpl, cur_desc);

				if (!iter->hw_next)
					break;
			}

			local_irq_save(flags);
			chan_last_sub[chan->device->id] = iter;
			local_irq_restore(flags);

		} else {
			/* first peer */
			cur_desc = chan->last_used->phys;

			local_irq_save(flags);
			chan_last_sub[chan->device->id] = chan->last_used;
			local_irq_restore(flags);

			/* set "do not generate interrupt" bit if necessary */
			if (!(chan->last_used->flags & PPC440SPE_DESC_INT))
				cur_desc |= DMA_CDB_NO_INT;
			/* put descriptor into FIFO */
			out_le32 (&dma_reg->cpfpl, cur_desc);
		}
		break;
	case PPC440SPE_XOR_ID:

		/* update h/w links and refetch */
		if (!xor_last_submit->hw_next)
			break;

		xor_reg = (xor_regs_t *)chan->device->pdev->resource[0].start;

		local_irq_save(flags);

		if (!(xor_reg->sr & XOR_SR_XCP_BIT)) {
			/* XORcore is idle. Refetch now */
			do_xor_refetch = 0;
			ppc440spe_xor_set_link(xor_last_submit, xor_last_submit->hw_next);
			xor_last_submit = xor_last_linked;
			xor_reg->crsr = XOR_CRSR_RCBE_BIT;
		} else {
			/* XORcore is running. Refetch later in the handler */
			do_xor_refetch = 1;
		}
		local_irq_restore(flags);

		break;
	}

	/* update watch-dog timer */
	mod_timer(&chan->cleanup_watchdog, jiffies +
					msecs_to_jiffies(PPC440SPE_ADMA_WATCHDOG_MSEC));
}

/**
 * ppc440spe_chan_get_current_descriptor - get the currently executed descriptor
 */
static inline u32 ppc440spe_chan_get_current_descriptor(ppc440spe_ch_t *chan)
{
	volatile dma_regs_t *dma_reg;
	volatile xor_regs_t *xor_reg;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		dma_reg = (dma_regs_t *)chan->device->pdev->resource[0].start;
		return (le32_to_cpu(dma_reg->acpl)) & (~DMA_CDB_MASK);
	case PPC440SPE_XOR_ID:
		xor_reg = (xor_regs_t *)chan->device->pdev->resource[0].start;
		return xor_reg->ccbalr;
	}
	return 0;
}

/**
 * ppc440spe_chan_run - enable the channel
 */
static inline void ppc440spe_chan_run(ppc440spe_ch_t *chan)
{
        volatile xor_regs_t *xor_reg;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		/* DMAs are always enabled, do nothing */
		break;
	case PPC440SPE_XOR_ID:
		/* drain write buffer */
		xor_reg = (xor_regs_t *)chan->device->pdev->resource[0].start;

		/* fetch descriptor pointed to in <link> */
		xor_reg->crsr = XOR_CRSR_XAE_BIT;
		break;
	}
}


/******************************************************************************
 * ADMA device level
 ******************************************************************************/

/**
 * ppc440spe_adma_free_slots - flags descriptor slots for reuse
 * @slot: Slot to free
 * Caller must hold &ppc440spe_chan->lock while calling this function
 */
static void ppc440spe_adma_free_slots(ppc440spe_desc_t *slot)
{
	int stride = slot->stride;

	while (stride--) {
		slot->stride = 0;
		slot = list_entry(slot->slot_node.next,
				ppc440spe_desc_t,
				slot_node);
	}
}

static dma_cookie_t ppc440spe_adma_run_tx_complete_actions(
		ppc440spe_desc_t *desc,
		ppc440spe_ch_t *chan,
		dma_cookie_t cookie)
{

	BUG_ON(desc->async_tx.cookie < 0);
	spin_lock_bh(&desc->async_tx.lock);
	if (desc->async_tx.cookie > 0) {
		cookie = desc->async_tx.cookie;
		desc->async_tx.cookie = 0;

		/* call the callback (must not sleep or submit new
		 * operations to this channel)
		 */
		if (desc->async_tx.callback)
			desc->async_tx.callback(
				desc->async_tx.callback_param);

		/* unmap dma addresses
		 * (unmap_single vs unmap_page?)
		 */
		if (desc->group_head && desc->async_tx.type != DMA_INTERRUPT) {
			ppc440spe_desc_t *unmap = desc->group_head;
			u32 src_cnt = unmap->unmap_src_cnt;
			dma_addr_t addr = ppc440spe_desc_get_dest_addr(unmap,
				chan);

			dma_unmap_page(&chan->device->pdev->dev, addr,
					unmap->unmap_len, DMA_FROM_DEVICE);
			while(src_cnt--) {
				addr = ppc440spe_desc_get_src_addr(unmap,
							chan,
							src_cnt);
				dma_unmap_page(&chan->device->pdev->dev, addr,
					unmap->unmap_len, DMA_TO_DEVICE);
			}
			desc->group_head = NULL;
		}
	}

	/* run dependent operations */
	async_tx_run_dependencies(&desc->async_tx, &chan->common);
	spin_unlock_bh(&desc->async_tx.lock);

	return cookie;
}

static int ppc440spe_adma_clean_slot(ppc440spe_desc_t *desc,
		ppc440spe_ch_t *chan)
{
	/* the client is allowed to attach dependent operations
	 * until 'ack' is set
	 */
	if (!desc->async_tx.ack)
		return 0;

	/* leave the last descriptor in the chain
	 * so we can append to it
	 */
	if (desc->chain_node.next == &chan->chain ||
			desc->phys == ppc440spe_chan_get_current_descriptor(chan))
		return 1;

	PRINTK("\tfree slot %x: %d stride: %d\n", desc->phys, desc->idx, desc->stride);

	list_del(&desc->chain_node);

	ppc440spe_adma_free_slots(desc);

	return 0;
}

static void __ppc440spe_adma_slot_cleanup(ppc440spe_ch_t *chan)
{
	ppc440spe_desc_t *iter, *_iter, *group_start = NULL;
	dma_cookie_t cookie = 0;
	u32 current_desc = ppc440spe_chan_get_current_descriptor(chan);
	int busy = ppc440spe_chan_is_busy(chan);
	int seen_current = 0, slot_cnt = 0, slots_per_op = 0;


	PRINTK ("ppc440spe adma%d: %s\n", chan->device->id, __FUNCTION__);

	/* free completed slots from the chain starting with
	 * the oldest descriptor
	 */
	list_for_each_entry_safe(iter, _iter, &chan->chain,
					chain_node) {
		PRINTK ("\tcookie: %d slot: %d busy: %d "
			"this_desc: %#x next_desc: %#x cur: %#x ack: %d\n",
			iter->async_tx.cookie, iter->idx, busy, iter->phys,
			ppc440spe_desc_get_link(iter, chan),
			current_desc,
			iter->async_tx.ack);

		/* do not advance past the current descriptor loaded into the
		 * hardware channel,subsequent descriptors are either in process
		 * or have not been submitted
		 */
		if (seen_current)
			break;

		/* stop the search if we reach the current descriptor and the
		 * channel is busy, or if it appears that the current descriptor
		 * needs to be re-read (i.e. has been appended to)
		 */
		if (iter->phys == current_desc) {
			BUG_ON(seen_current++);
			if (busy || ppc440spe_desc_get_link(iter, chan)) {
				ppc440spe_adma_run_tx_complete_actions(iter,
					chan, cookie);
				break;
			}
		}

		/* detect the start of a group transaction */
		if (!slot_cnt && !slots_per_op) {
			slot_cnt = iter->slot_cnt;
			slots_per_op = iter->slots_per_op;
			if (slot_cnt <= slots_per_op) {
				slot_cnt = 0;
				slots_per_op = 0;
			}
		}

		if (slot_cnt) {
			PRINTK("\tgroup++\n");
			if (!group_start)
				group_start = iter;
			slot_cnt -= slots_per_op;
		}

		/* all the members of a group are complete */
		if (slots_per_op != 0 && slot_cnt == 0) {
			ppc440spe_desc_t *grp_iter, *_grp_iter;
			int end_of_chain = 0;
			PRINTK("\tgroup end\n");

			/* collect the total results */
			if (group_start->xor_check_result) {
				u32 zero_sum_result = 0;
				slot_cnt = group_start->slot_cnt;
				grp_iter = group_start;

				list_for_each_entry_from(grp_iter,
					&chan->chain, chain_node) {
					slot_cnt -= slots_per_op;
					if (slot_cnt == 0)
						break;
				}
				PRINTK("\tgroup_start->xor_check_result: %p\n",
					group_start->xor_check_result);
				*group_start->xor_check_result = zero_sum_result;
			}

			/* clean up the group */
			slot_cnt = group_start->slot_cnt;
			grp_iter = group_start;
			list_for_each_entry_safe_from(grp_iter, _grp_iter,
				&chan->chain, chain_node) {

				cookie = ppc440spe_adma_run_tx_complete_actions(
					grp_iter, chan, cookie);

				slot_cnt -= slots_per_op;
				end_of_chain = ppc440spe_adma_clean_slot(grp_iter,
					chan);

				if (slot_cnt == 0 || end_of_chain)
					break;
			}

			/* the group should be complete at this point */
			BUG_ON(slot_cnt);

			slots_per_op = 0;
			group_start = NULL;
			if (end_of_chain)
				break;
			else
				continue;
		} else if (slots_per_op) /* wait for group completion */
			continue;

		cookie = ppc440spe_adma_run_tx_complete_actions(iter, chan, cookie);

		if (ppc440spe_adma_clean_slot(iter, chan))
			break;
	}

	if (!seen_current) {
		BUG();
	}

	ppc440spe_chan_idle(busy, chan);

	if (cookie > 0) {
		chan->completed_cookie = cookie;
		PRINTK("\tcompleted cookie %d\n", cookie);
	}

}

static void ppc440spe_adma_tasklet (unsigned long data)
{
        ppc440spe_ch_t *chan = (ppc440spe_ch_t *) data;
        __ppc440spe_adma_slot_cleanup(chan);
}

static void ppc440spe_adma_slot_cleanup (ppc440spe_ch_t *chan)
{
	spin_lock_bh(&chan->lock);
	__ppc440spe_adma_slot_cleanup(chan);
	spin_unlock_bh(&chan->lock);
}

static ppc440spe_desc_t *ppc440spe_adma_alloc_slots(
		ppc440spe_ch_t *chan, int num_slots,
		int slots_per_op)
{
	ppc440spe_desc_t *iter = NULL, *alloc_start = NULL;
	ppc440spe_desc_t *last_used = NULL, *last_op_head = NULL;
	struct list_head chain = LIST_HEAD_INIT(chain);
	int i;

	/* start search from the last allocated descrtiptor
	 * if a contiguous allocation can not be found start searching
	 * from the beginning of the list
	 */

	for (i = 0; i < 2; i++) {
		int slots_found = 0;
		if (i == 0)
			iter = chan->last_used;
		else {
			iter = list_entry(&chan->all_slots,
				ppc440spe_desc_t,
				slot_node);
		}

		list_for_each_entry_continue(iter, &chan->all_slots, slot_node) {
			if (iter->stride) {
				/* give up after finding the first busy slot
				 * on the second pass through the list
				 */
				if (i == 1)
					break;

				slots_found = 0;
				continue;
			}

			/* start the allocation if the slot is correctly aligned */
			if (!slots_found++) {
				if (ppc440spe_desc_is_aligned(iter, slots_per_op))
					alloc_start = iter;
				else {
					slots_found = 0;
					continue;
				}
			}

			if (slots_found == num_slots) {
				iter = alloc_start;
				i = 0;
				while (num_slots) {

					/* pre-ack all but the last descriptor */
					if (num_slots != slots_per_op)
						iter->async_tx.ack = 1;
					else
						iter->async_tx.ack = 0;

					PRINTK ("ppc440spe adma%d: allocated slot: %d "
						"(desc %p phys: %#x) stride %d"
						",ack = %d\n",
						chan->device->id,
						iter->idx, iter->hw_desc, iter->phys,
						slots_per_op, iter->async_tx.ack);

					list_add_tail(&iter->chain_node, &chain);
					last_op_head = iter;
					iter->async_tx.cookie = 0;
					iter->hw_next = NULL;
					iter->flags = 0;
					iter->slot_cnt = num_slots;
					iter->slots_per_op = slots_per_op;
					iter->xor_check_result = NULL;
					for (i = 0; i < slots_per_op; i++) {
						iter->stride = slots_per_op - i;
						last_used = iter;
						iter = list_entry(
							iter->slot_node.next,
							ppc440spe_desc_t,
							slot_node);
					}
					num_slots -= slots_per_op;
				}
				last_op_head->group_head = alloc_start;
				last_op_head->async_tx.cookie = -EBUSY;
				list_splice(&chain, &last_op_head->group_list);
				chan->last_used = last_used;
				return last_op_head;
			}
		}
	}

	/* try to free some slots if the allocation fails */
	tasklet_schedule(&chan->irq_tasklet);
	return NULL;
}

/* returns the actual number of allocated descriptors */
static int ppc440spe_adma_alloc_chan_resources(struct dma_chan *chan)
{
	ppc440spe_ch_t *ppc440spe_chan = to_ppc440spe_adma_chan(chan);
	ppc440spe_desc_t *slot = NULL;
	char *hw_desc;
	int i, db_sz;
	int init = ppc440spe_chan->slots_allocated ? 0 : 1;
	ppc440spe_aplat_t *plat_data;

	chan->chan_id = ppc440spe_chan->device->id;
	plat_data = ppc440spe_chan->device->pdev->dev.platform_data;

	/* Allocate descriptor slots */
	i = ppc440spe_chan->slots_allocated;
	if (ppc440spe_chan->device->id != PPC440SPE_XOR_ID)
		db_sz = sizeof (dma_cdb_t);
	else
		db_sz = sizeof (xor_cb_t);

	for (; i < (plat_data->pool_size/db_sz); i++) {
		slot = kzalloc(sizeof(ppc440spe_desc_t), GFP_KERNEL);
		if (!slot) {
			printk(KERN_INFO "SPE ADMA Channel only initialized"
				" %d descriptor slots", i--);
			break;
		}

		hw_desc = (char *) ppc440spe_chan->device->dma_desc_pool_virt;
		slot->hw_desc = (void *) &hw_desc[i * db_sz];
		dma_async_tx_descriptor_init(&slot->async_tx, chan);
		INIT_LIST_HEAD(&slot->chain_node);
		INIT_LIST_HEAD(&slot->slot_node);
		INIT_LIST_HEAD(&slot->group_list);
		hw_desc = (char *) ppc440spe_chan->device->dma_desc_pool;
		slot->phys = (dma_addr_t) &hw_desc[i * db_sz];
		slot->idx = i;

		spin_lock_bh(&ppc440spe_chan->lock);
		ppc440spe_chan->slots_allocated++;
		list_add_tail(&slot->slot_node, &ppc440spe_chan->all_slots);
		spin_unlock_bh(&ppc440spe_chan->lock);
	}

	if (i && !ppc440spe_chan->last_used) {
		ppc440spe_chan->last_used =
			list_entry(ppc440spe_chan->all_slots.next,
				ppc440spe_desc_t,
				slot_node);
	}

	PRINTK("ppc440spe adma%d: allocated %d descriptor slots\n",
		ppc440spe_chan->device->id, i);

	/* initialize the channel and the chain with a null operation */
	if (init) {
		if (test_bit(DMA_XOR,
			&ppc440spe_chan->device->common.capabilities))
			ppc440spe_chan_start_null_xor(ppc440spe_chan);
	}

	return (i > 0) ? i : -ENOMEM;
}

static dma_cookie_t ppc440spe_desc_assign_cookie(ppc440spe_ch_t *chan,
		ppc440spe_desc_t *desc)
{
	dma_cookie_t cookie = chan->common.cookie;
	cookie++;
	if (cookie < 0)
		cookie = 1;
	chan->common.cookie = desc->async_tx.cookie = cookie;
	return cookie;
}

static void ppc440spe_adma_check_threshold(ppc440spe_ch_t *chan)
{
	PRINTK("ppc440spe adma%d: pending: %d\n", chan->device->id, chan->pending);

	if (chan->pending >= PPC440SPE_ADMA_THRESHOLD) {
		chan->pending = 0;
		ppc440spe_chan_append(chan);
	}
}


static dma_cookie_t ppc440spe_adma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	ppc440spe_desc_t *sw_desc = tx_to_ppc440spe_adma_slot(tx);
	ppc440spe_ch_t *chan = to_ppc440spe_adma_chan(tx->chan);
	ppc440spe_desc_t *group_start, *old_chain_tail;
	int slot_cnt;
	int slots_per_op;
	dma_cookie_t cookie;

	group_start = sw_desc->group_head;
	slot_cnt = group_start->slot_cnt;
	slots_per_op = group_start->slots_per_op;

	spin_lock_bh(&chan->lock);

	cookie = ppc440spe_desc_assign_cookie(chan, sw_desc);

	old_chain_tail = list_entry(chan->chain.prev,
		ppc440spe_desc_t, chain_node);
	list_splice_init(&sw_desc->group_list, &old_chain_tail->chain_node);

	/* fix up the hardware chain */
	ppc440spe_desc_set_link(chan, old_chain_tail, group_start);

	/* increment the pending count by the number of operations */
	chan->pending += slot_cnt / slots_per_op;
	ppc440spe_adma_check_threshold(chan);
	spin_unlock_bh(&chan->lock);

	PRINTK("ppc440spe adma%d: %s cookie: %d slot: %d tx %p\n",
		chan->device->id,__FUNCTION__,
		sw_desc->async_tx.cookie, sw_desc->idx, sw_desc);

	return cookie;
}

struct dma_async_tx_descriptor *ppc440spe_adma_prep_dma_interrupt(
		struct dma_chan *chan)
{
	ppc440spe_ch_t *ppc440spe_chan = to_ppc440spe_adma_chan(chan);
	ppc440spe_desc_t *sw_desc, *group_start;
	int slot_cnt, slots_per_op;

	PRINTK("ppc440spe adma%d: %s\n", ppc440spe_chan->device->id,
		__FUNCTION__);

	spin_lock_bh(&ppc440spe_chan->lock);
	slot_cnt = slots_per_op = 1;
	sw_desc = ppc440spe_adma_alloc_slots(ppc440spe_chan, slot_cnt,
			slots_per_op);
	if (sw_desc) {
		group_start = sw_desc->group_head;
		ppc440spe_desc_init_interrupt(group_start, ppc440spe_chan);
		sw_desc->async_tx.type = DMA_INTERRUPT;
	}
	spin_unlock_bh(&ppc440spe_chan->lock);

	return sw_desc ? &sw_desc->async_tx : NULL;
}

struct dma_async_tx_descriptor *ppc440spe_adma_prep_dma_memcpy(
		struct dma_chan *chan, size_t len, int int_en)
{
	ppc440spe_ch_t *ppc440spe_chan = to_ppc440spe_adma_chan(chan);
	ppc440spe_desc_t *sw_desc, *group_start;
	int slot_cnt, slots_per_op;
	if (unlikely(!len))
		return NULL;
	BUG_ON(unlikely(len > PPC440SPE_ADMA_MAX_BYTE_COUNT));

	spin_lock_bh(&ppc440spe_chan->lock);

	PRINTK("ppc440spe adma%d: %s len: %u int_en %d\n",
	ppc440spe_chan->device->id, __FUNCTION__, len, int_en);

	slot_cnt = slots_per_op = 1;
	sw_desc = ppc440spe_adma_alloc_slots(ppc440spe_chan, slot_cnt,
		slots_per_op);
	if (sw_desc) {
		group_start = sw_desc->group_head;
		ppc440spe_desc_init_memcpy(group_start, int_en);
		ppc440spe_desc_set_byte_count(group_start, ppc440spe_chan, len);
		sw_desc->unmap_src_cnt = 1;
		sw_desc->unmap_len = len;
		sw_desc->async_tx.type = DMA_MEMCPY;
	}
	spin_unlock_bh(&ppc440spe_chan->lock);

	return sw_desc ? &sw_desc->async_tx : NULL;
}

struct dma_async_tx_descriptor *ppc440spe_adma_prep_dma_xor(
		struct dma_chan *chan, unsigned int src_cnt, size_t len,
		int int_en)
{
	ppc440spe_ch_t *ppc440spe_chan = to_ppc440spe_adma_chan(chan);
	ppc440spe_desc_t *sw_desc, *group_start;
	int slot_cnt, slots_per_op;
	if (unlikely(!len))
		return NULL;
	BUG_ON(unlikely(len > PPC440SPE_ADMA_XOR_MAX_BYTE_COUNT));

	PRINTK("ppc440spe adma%d: %s src_cnt: %d len: %u int_en: %d\n",
	ppc440spe_chan->device->id, __FUNCTION__, src_cnt, len, int_en);

	spin_lock_bh(&ppc440spe_chan->lock);
	slot_cnt = ppc440spe_chan_xor_slot_count(len, src_cnt, &slots_per_op);
	sw_desc = ppc440spe_adma_alloc_slots(ppc440spe_chan, slot_cnt,
			slots_per_op);
	if (sw_desc) {
		group_start = sw_desc->group_head;
		ppc440spe_desc_init_xor(group_start, src_cnt, int_en);
		ppc440spe_desc_set_byte_count(group_start, ppc440spe_chan, len);
		sw_desc->unmap_src_cnt = src_cnt;
		sw_desc->unmap_len = len;
		sw_desc->async_tx.type = DMA_XOR;
	}
	spin_unlock_bh(&ppc440spe_chan->lock);

	return sw_desc ? &sw_desc->async_tx : NULL;
}

static void ppc440spe_adma_set_dest(dma_addr_t addr,
		struct dma_async_tx_descriptor *tx, int index)
{
	ppc440spe_desc_t *sw_desc = tx_to_ppc440spe_adma_slot(tx);
	ppc440spe_ch_t *chan = to_ppc440spe_adma_chan(tx->chan);

	/* to do: support transfers lengths > PPC440SPE_ADMA_MAX_BYTE_COUNT */
	ppc440spe_desc_set_dest_addr(sw_desc->group_head, chan,  addr);
}

static void ppc440spe_adma_set_src(dma_addr_t addr,
		struct dma_async_tx_descriptor *tx,
		int index)
{
	ppc440spe_ch_t *chan = to_ppc440spe_adma_chan(tx->chan);

	ppc440spe_desc_t *sw_desc = tx_to_ppc440spe_adma_slot(tx);
	ppc440spe_desc_t *group_start = sw_desc->group_head;

	switch (tx->type) {
	case DMA_MEMCPY:
	case DMA_XOR:
		ppc440spe_desc_set_src_addr(group_start, chan, index, addr);
		break;
	/* todo: case DMA_ZERO_SUM: */
	/* todo: case DMA_PQ_XOR: */
	/* todo: case DMA_DUAL_XOR: */
	/* todo: case DMA_PQ_UPDATE: */
	/* todo: case DMA_PQ_ZERO_SUM: */
	/* todo: case DMA_MEMCPY_CRC32C: */
	case DMA_MEMSET:
	default:
		printk(KERN_ERR "ppc440spe adma%d:unsupport tx_type: %d\n",
			chan->device->id, tx->type);
		BUG();
		break;
	}
}

static void ppc440spe_adma_dependency_added(struct dma_chan *chan)
{
	ppc440spe_ch_t *ppc440spe_chan = to_ppc440spe_adma_chan(chan);
	tasklet_schedule(&ppc440spe_chan->irq_tasklet);
}

static void ppc440spe_adma_free_chan_resources(struct dma_chan *chan)
{
	ppc440spe_ch_t *ppc440spe_chan = to_ppc440spe_adma_chan(chan);
	ppc440spe_desc_t *iter, *_iter;
	int in_use_descs = 0;

	ppc440spe_adma_slot_cleanup(ppc440spe_chan);

	spin_lock_bh(&ppc440spe_chan->lock);
	list_for_each_entry_safe(iter, _iter, &ppc440spe_chan->chain,
					chain_node) {
		in_use_descs++;
		list_del(&iter->chain_node);
	}
	list_for_each_entry_safe_reverse(iter, _iter,
			&ppc440spe_chan->all_slots, slot_node) {
		list_del(&iter->slot_node);
		kfree(iter);
		ppc440spe_chan->slots_allocated--;
	}
	ppc440spe_chan->last_used = NULL;

	PRINTK("ppc440spe adma%d %s slots_allocated %d\n",
		ppc440spe_chan->device->id,
		__FUNCTION__, ppc440spe_chan->slots_allocated);
	spin_unlock_bh(&ppc440spe_chan->lock);

	/* one is ok since we left it on there on purpose */
	if (in_use_descs > 1)
		printk(KERN_ERR "SPE: Freeing %d in use descriptors!\n",
			in_use_descs - 1);
}

/**
 * ppc440spe_adma_is_complete - poll the status of an ADMA transaction
 * @chan: ADMA channel handle
 * @cookie: ADMA transaction identifier
 */
static enum dma_status ppc440spe_adma_is_complete(struct dma_chan *chan,
					    dma_cookie_t cookie,
					    dma_cookie_t *done,
					    dma_cookie_t *used)
{
	ppc440spe_ch_t *ppc440spe_chan = to_ppc440spe_adma_chan(chan);
	dma_cookie_t last_used;
	dma_cookie_t last_complete;
	enum dma_status ret;

	last_used = chan->cookie;
	last_complete = ppc440spe_chan->completed_cookie;

	if (done)
		*done= last_complete;
	if (used)
		*used = last_used;

	ret = dma_async_is_complete(cookie, last_complete, last_used);
	if (ret == DMA_SUCCESS)
		return ret;

	ppc440spe_adma_slot_cleanup(ppc440spe_chan);

	last_used = chan->cookie;
	last_complete = ppc440spe_chan->completed_cookie;

	if (done)
		*done= last_complete;
	if (used)
		*used = last_used;

	return dma_async_is_complete(cookie, last_complete, last_used);
}

/*
 * End of transfer interrupt handler
 */
static irqreturn_t ppc440spe_adma_eot_handler(int irq, void *data)
{
	ppc440spe_ch_t *chan = data;

	PRINTK("ppc440spe adma%d: %s\n", chan->device->id, __FUNCTION__);

	tasklet_schedule(&chan->irq_tasklet);
	ppc440spe_adma_device_clear_eot_status(chan);

	return IRQ_HANDLED;
}

static void ppc440spe_adma_issue_pending(struct dma_chan *chan)
{
	ppc440spe_ch_t *ppc440spe_chan = to_ppc440spe_adma_chan(chan);

	PRINTK("ppc440spe adma%d: %s %d \n",
			ppc440spe_chan->device->id, __FUNCTION__,
			ppc440spe_chan->pending);

	if (ppc440spe_chan->pending) {
		ppc440spe_chan->pending = 0;
		ppc440spe_chan_append(ppc440spe_chan);
	}
}

static int __devexit ppc440spe_adma_remove(struct platform_device *dev)
{
	ppc440spe_dev_t *device = platform_get_drvdata(dev);
	struct dma_chan *chan, *_chan;
	ppc440spe_ch_t *ppc440spe_chan;
	int i;
	ppc440spe_aplat_t *plat_data = dev->dev.platform_data;

	PRINTK("%s\n", __FUNCTION__);

	dma_async_device_unregister(&device->common);

	for (i = 0; i < 3; i++) {
		unsigned int irq;
		irq = platform_get_irq(dev, i);
		free_irq(irq, device);
	}

	dma_free_coherent(&dev->dev, plat_data->pool_size,
			device->dma_desc_pool_virt, device->dma_desc_pool);

	do {
		struct resource *res;
		res = platform_get_resource(dev, IORESOURCE_MEM, 0);
		release_mem_region(res->start, res->end - res->start);
	} while (0);

	list_for_each_entry_safe(chan, _chan, &device->common.channels,
				device_node) {
		ppc440spe_chan = to_ppc440spe_adma_chan(chan);
		list_del(&chan->device_node);
		kfree(ppc440spe_chan);
	}
	kfree(device);

	return 0;
}

static int __devinit ppc440spe_adma_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret=0, irq;
	ppc440spe_dev_t *adev;
	ppc440spe_ch_t *chan;
	ppc440spe_aplat_t *plat_data = pdev->dev.platform_data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	if (!request_mem_region(res->start, res->end - res->start, pdev->name))
		return -EBUSY;

	/* create a device */
	if ((adev = kzalloc(sizeof(*adev), GFP_KERNEL)) == NULL) {
		ret = -ENOMEM;
		goto err_adev_alloc;
	}

	/* allocate coherent memory for hardware descriptors
	 * note: writecombine gives slightly better performance, but
	 * requires that we explicitly drain the write buffer
	 */
	if ((adev->dma_desc_pool_virt = dma_alloc_coherent(&pdev->dev,
					plat_data->pool_size,
					&adev->dma_desc_pool,
					GFP_KERNEL)) == NULL) {
		ret = -ENOMEM;
		goto err_dma_alloc;
	}

	PRINTK("%s: allocted descriptor pool virt %p phys %p\n",
		__FUNCTION__, adev->dma_desc_pool_virt,
		(void *) adev->dma_desc_pool);

	adev->id = plat_data->hw_id;
	adev->common.capabilities = plat_data->capabilities;
	adev->pdev = pdev;
	platform_set_drvdata(pdev, adev);

	INIT_LIST_HEAD(&adev->common.channels);

	/* set base routines */
	adev->common.device_tx_submit = ppc440spe_adma_tx_submit;
	adev->common.device_set_dest = ppc440spe_adma_set_dest;
	adev->common.device_set_src = ppc440spe_adma_set_src;
	adev->common.device_alloc_chan_resources = ppc440spe_adma_alloc_chan_resources;
	adev->common.device_free_chan_resources = ppc440spe_adma_free_chan_resources;
	adev->common.device_is_tx_complete = ppc440spe_adma_is_complete;
	adev->common.device_issue_pending = ppc440spe_adma_issue_pending;
	adev->common.device_dependency_added = ppc440spe_adma_dependency_added;
	adev->common.dev = &pdev->dev;

	/* set prep routines based on capability */
	if (test_bit(DMA_MEMCPY, &adev->common.capabilities)) {
		adev->common.device_prep_dma_memcpy = ppc440spe_adma_prep_dma_memcpy;
	}
	if (test_bit(DMA_XOR, &adev->common.capabilities)) {
		adev->common.max_xor = XOR_MAX_OPS;
		adev->common.device_prep_dma_xor = ppc440spe_adma_prep_dma_xor;
	}
	if (test_bit(DMA_INTERRUPT, &adev->common.capabilities)) {
		adev->common.device_prep_dma_interrupt = ppc440spe_adma_prep_dma_interrupt;
	}

	/* create a channel */
	if ((chan = kzalloc(sizeof(*chan), GFP_KERNEL)) == NULL) {
		ret = -ENOMEM;
		goto err_chan_alloc;
	}

	tasklet_init(&chan->irq_tasklet, ppc440spe_adma_tasklet, (unsigned long)chan);
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = -ENXIO;
	} else {
		ret = request_irq(irq, ppc440spe_adma_eot_handler,
			0, pdev->name, chan);
		if (ret) {
			ret = -EIO;
			goto err_irq;
		}
	}

	chan->device = adev;
	spin_lock_init(&chan->lock);
	init_timer(&chan->cleanup_watchdog);
	chan->cleanup_watchdog.data = (unsigned long) chan;
	chan->cleanup_watchdog.function = ppc440spe_adma_tasklet;
	INIT_LIST_HEAD(&chan->chain);
	INIT_LIST_HEAD(&chan->all_slots);
	INIT_RCU_HEAD(&chan->common.rcu);
	chan->common.device = &adev->common;
	list_add_tail(&chan->common.device_node, &adev->common.channels);

	printk(KERN_INFO "AMCC(R) PPC440SPE ADMA Engine found [%d]: "
	  "( %s%s%s%s%s%s%s%s%s%s)\n",
	  adev->id,
	  test_bit(DMA_PQ_XOR, &adev->common.capabilities) ? "pq_xor " : "",
	  test_bit(DMA_PQ_UPDATE, &adev->common.capabilities) ? "pq_update " : "",
	  test_bit(DMA_PQ_ZERO_SUM, &adev->common.capabilities) ? "pq_zero_sum " : "",
	  test_bit(DMA_XOR, &adev->common.capabilities) ? "xor " : "",
	  test_bit(DMA_DUAL_XOR, &adev->common.capabilities) ? "dual_xor " : "",
	  test_bit(DMA_ZERO_SUM, &adev->common.capabilities) ? "xor_zero_sum " : "",
	  test_bit(DMA_MEMSET, &adev->common.capabilities)  ? "memset " : "",
	  test_bit(DMA_MEMCPY_CRC32C, &adev->common.capabilities) ? "memcpy+crc " : "",
	  test_bit(DMA_MEMCPY, &adev->common.capabilities) ? "memcpy " : "",
	  test_bit(DMA_INTERRUPT, &adev->common.capabilities) ? "int " : "");

	dma_async_device_register(&adev->common);
	goto out;

err_irq:
	kfree(chan);
err_chan_alloc:
	dma_free_coherent(&adev->pdev->dev, plat_data->pool_size,
			adev->dma_desc_pool_virt, adev->dma_desc_pool);
err_dma_alloc:
	kfree(adev);
err_adev_alloc:
	release_mem_region(res->start, res->end - res->start);
out:
	return ret;
}

static void ppc440spe_chan_start_null_xor(ppc440spe_ch_t *chan)
{
	ppc440spe_desc_t *sw_desc, *group_start;
	dma_cookie_t cookie;
	int slot_cnt, slots_per_op;

	PRINTK("ppc440spe adma%d: %s\n", chan->device->id, __FUNCTION__);

	spin_lock_bh(&chan->lock);
	slot_cnt = ppc440spe_chan_xor_slot_count(0, 2, &slots_per_op);
	sw_desc = ppc440spe_adma_alloc_slots(chan, slot_cnt, slots_per_op);
	if (sw_desc) {
		group_start = sw_desc->group_head;
		list_splice_init(&sw_desc->group_list, &chan->chain);
		sw_desc->async_tx.ack = 1;
		ppc440spe_desc_init_null_xor(group_start);

		cookie = chan->common.cookie;
		cookie++;
		if (cookie <= 1)
			cookie = 2;

		/* initialize the completed cookie to be less than
		 * the most recently used cookie
		 */
		chan->completed_cookie = cookie - 1;
		chan->common.cookie = sw_desc->async_tx.cookie = cookie;

		/* channel should not be busy */
		BUG_ON(ppc440spe_chan_is_busy(chan));

		/* set the descriptor address */
		ppc440spe_chan_set_first_xor_descriptor(chan, sw_desc);

		/* run the descriptor */
		ppc440spe_chan_run(chan);
	} else
		printk(KERN_ERR "ppc440spe adma%d"
			" failed to allocate null descriptor\n",
			chan->device->id);
	spin_unlock_bh(&chan->lock);
}

static struct platform_driver ppc440spe_adma_driver = {
	.probe		= ppc440spe_adma_probe,
	.remove		= ppc440spe_adma_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "PPC440SPE-ADMA",
	},
};

static int __init ppc440spe_adma_init (void)
{
	/* it's currently unsafe to unload this module */
	/* if forced, worst case is that rmmod hangs */
	__unsafe(THIS_MODULE);

	return platform_driver_register(&ppc440spe_adma_driver);
}

static void __exit ppc440spe_adma_exit (void)
{
	platform_driver_unregister(&ppc440spe_adma_driver);
	return;
}

module_init(ppc440spe_adma_init);
module_exit(ppc440spe_adma_exit);

MODULE_AUTHOR("Yuri Tikhonov <yur@emcraft.com>");
MODULE_DESCRIPTION("PPC440SPE ADMA Engine Driver");
MODULE_LICENSE("GPL");
