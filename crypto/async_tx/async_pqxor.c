/*
 *	Copyright(c) 2007 Yuri Tikhonov <yur@emcraft.com>
 *
 *	Developed for DENX Software Engineering GmbH
 *
 *	Asynchronous GF-XOR calculations ASYNC_TX API.
 *
 *	based on async_xor.c code written by:
 *		Dan Williams <dan.j.williams@intel.com>
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
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/raid/xor.h>
#include <linux/async_tx.h>

#include "../drivers/md/raid6.h"

/**
 * do_async_pqxor - asynchronously calculate P and/or Q
 */
static void
do_async_pqxor(struct dma_async_tx_descriptor *tx, struct dma_device *device,
	struct dma_chan *chan,
	struct page *pdest, struct page *qdest,
	struct page **src_list, unsigned char *scoef_list,
	unsigned int offset, unsigned int src_cnt, size_t len,
	enum async_tx_flags flags, struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback callback, void *callback_param)
{
	struct page *dest;
	dma_addr_t dma_addr;
	enum dma_data_direction dir;
	int i;

	/*  One parity (P or Q) calculation is initiated always;
	 * first always try Q
	 */
	dir = (flags & ASYNC_TX_ASSUME_COHERENT) ?
		DMA_NONE : DMA_FROM_DEVICE;
	dest = qdest ? qdest : pdest;
	dma_addr = dma_map_page(device->dev, dest, offset, len, dir);
	tx->tx_set_dest(dma_addr, tx, 0);

	/* Switch to the next destination */
	if (qdest && pdest) {
		/* Both destinations are set, thus here we deal with P */
		dma_addr = dma_map_page(device->dev, pdest, offset, len, dir);
		tx->tx_set_dest(dma_addr, tx, 1);
	}

	dir = (flags & ASYNC_TX_ASSUME_COHERENT) ?
		DMA_NONE : DMA_TO_DEVICE;
	for (i = 0; i < src_cnt; i++) {
		dma_addr = dma_map_page(device->dev, src_list[i],
			offset, len, dir);
		tx->tx_set_src(dma_addr, tx, i);
		if (!qdest)
			/* P-only calculation */
			tx->tx_set_src_mult(1, tx, i);
		else
			/* PQ or Q-only calculation */
			tx->tx_set_src_mult(scoef_list[i], tx, i);
	}

	async_tx_submit(chan, tx, flags, depend_tx, callback,
		callback_param);
}

/**
 * do_sync_pqxor - synchronously calculate P and Q
 */
static void
do_sync_pqxor(struct page *pdest, struct page *qdest,
	struct page **src_list, unsigned int offset,
	unsigned int src_cnt, size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback callback, void *callback_param)
{
	int i;

	/* reuse the 'src_list' array to convert to buffer pointers */
	for (i = 0; i < src_cnt; i++)
		src_list[i] = (struct page *)
			(page_address(src_list[i]) + offset);

	/* set destination addresses */
	src_list[i++] = (struct page *)(page_address(pdest) + offset);
	src_list[i++] = (struct page *)(page_address(qdest) + offset);

	if (flags & ASYNC_TX_XOR_ZERO_DST) {
		memset(src_list[i-2], 0, len);
		memset(src_list[i-1], 0, len);
	}

	raid6_call.gen_syndrome(i, len, (void **)src_list);
	async_tx_sync_epilog(flags, depend_tx, callback, callback_param);
}

#if defined(CONFIG_440SPE) || defined(CONFIG_440SP)
struct dma_chan *ppc440spe_get_best_pqchan (struct page **srcs, int src_cnt,
	size_t len);
#endif

/**
 * async_pqxor - attempt to calculate RS-syndrome and XOR in parallel using
 *	a dma engine.
 * @pdest: destination page for P-parity (XOR)
 * @qdest: destination page for Q-parity (GF-XOR)
 * @src_list: array of source pages
 * @src_coef_list: array of source coefficients used in GF-multiplication
 * @offset: offset in pages to start transaction
 * @src_cnt: number of source pages
 * @len: length in bytes
 * @flags: ASYNC_TX_XOR_ZERO_DST, ASYNC_TX_ASSUME_COHERENT,
 *	ASYNC_TX_ACK, ASYNC_TX_DEP_ACK, ASYNC_TX_ASYNC_ONLY
 * @depend_tx: depends on the result of this transaction.
 * @callback: function to call when the operation completes
 * @callback_param: parameter to pass to the callback routine
 */
struct dma_async_tx_descriptor *
async_pqxor(struct page *pdest, struct page *qdest,
	struct page **src_list, unsigned char *scoef_list,
	unsigned int offset, int src_cnt, size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback callback, void *callback_param)
{
#if defined(CONFIG_440SPE) || defined(CONFIG_440SP)
	struct dma_chan *chan = ppc440spe_get_best_pqchan(src_list, src_cnt, len);
#else
	struct dma_chan *chan = async_tx_find_channel(depend_tx, DMA_PQ_XOR);
#endif
	struct dma_device *device = chan ? chan->device : NULL;
	struct dma_async_tx_descriptor *tx = NULL;
	int int_en;

	if (!device && (flags & ASYNC_TX_ASYNC_ONLY))
		return NULL;

	if (device) { /* run the xor asynchronously */
		int_en = callback ? 1 : 0;

		tx = device->device_prep_dma_pqxor(chan,
			src_list, src_cnt,
			(pdest && qdest) ? 2 : 1,
			len,
			flags & ASYNC_TX_XOR_ZERO_DST ? 1 : 0,
			int_en);

		if (tx) {
			do_async_pqxor(tx, device, chan,
				pdest, qdest,
				src_list, scoef_list,
				offset, src_cnt, len,
				flags, depend_tx, callback,
				callback_param);
		}  else /* fall through */ {
			if (flags & ASYNC_TX_ASYNC_ONLY)
				return NULL;
			goto qxor_sync;
		}
	} else { /* run the pqxor synchronously */
qxor_sync:
		/* may do synchronous PQ only when both destinations exsists */
		if (!pdest || !qdest)
			return NULL;

		/* wait for any prerequisite operations */
		if (depend_tx) {
			/* if ack is already set then we cannot be sure
			 * we are referring to the correct operation
			 */
			BUG_ON(depend_tx->ack);
			if (dma_wait_for_async_tx(depend_tx) == DMA_ERROR)
				panic("%s: DMA_ERROR waiting for depend_tx\n",
					__FUNCTION__);
		}

		do_sync_pqxor(pdest, qdest, src_list,
			offset,	src_cnt, len, flags, depend_tx,
			callback, callback_param);
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_pqxor);

static int page_is_zero(struct page *p, size_t len)
{
	char *a;

	BUG_ON(!p);
	a = page_address(p);
	return ((*(u32*)a) == 0 &&
		memcmp(a, a+4, len-4)==0);
}


/**
 * async_xor_zero_sum - attempt a PQ parities check with a dma engine.
 * @pdest: P-parity destination to check
 * @qdest: Q-parity destination to check
 * @src_list: array of source pages.
 * @scoef_list: coefficients to use in GF-multiplications
 * @offset: offset in pages to start transaction
 * @src_cnt: number of source pages
 * @len: length in bytes
 * @presult: 0 if P parity is OK else non-zero
 * @qresult: 0 if Q parity is OK else non-zero
 * @flags: ASYNC_TX_ASSUME_COHERENT, ASYNC_TX_ACK, ASYNC_TX_DEP_ACK
 * @depend_tx: depends on the result of this transaction.
 * @callback: function to call when the xor completes
 * @callback_param: parameter to pass to the callback routine
 */
struct dma_async_tx_descriptor *
async_pqxor_zero_sum(struct page *pdest, struct page *qdest,
	struct page **src_list, unsigned char *scoef_list,
	unsigned int offset, int src_cnt, size_t len,
	u32 *presult, u32 *qresult, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback callback, void *callback_param)
{
	struct dma_chan *chan = async_tx_find_channel(depend_tx, DMA_PQ_ZERO_SUM);
	struct dma_device *device = chan ? chan->device : NULL;
	struct page *dest;
	int int_en = callback ? 1 : 0;
	struct dma_async_tx_descriptor *tx = device ?
		device->device_prep_dma_pqzero_sum(chan,
			src_cnt, (pdest && qdest) ? 2 : 1, len,
			presult, qresult, int_en) : NULL;
	int i;

	BUG_ON(src_cnt <= 1);

	if (tx) {
		dma_addr_t dma_addr;
		enum dma_data_direction dir;

		dir = (flags & ASYNC_TX_ASSUME_COHERENT) ?
			DMA_NONE : DMA_TO_DEVICE;

		/* Set location of first parity to check;
		 * first try Q
		 */
		dest = qdest ? qdest : pdest;
		dma_addr = dma_map_page(device->dev, dest, offset, len, dir);
		tx->tx_set_dest(dma_addr, tx, 0);

		if (qdest && pdest) {
			/* Both parities has to be checked */
			dma_addr = dma_map_page(device->dev, pdest, offset, len, dir);
			tx->tx_set_dest(dma_addr, tx, 1);
		}

		dir = (flags & ASYNC_TX_ASSUME_COHERENT) ?
			DMA_NONE : DMA_TO_DEVICE;

		/* Set location of sources and coefficient form these parities */
		for (i = 0; i < src_cnt; i++) {
			dma_addr = dma_map_page(device->dev, src_list[i],
				offset, len, dir);
			tx->tx_set_src(dma_addr, tx, i);
			tx->tx_set_src_mult(scoef_list[i], tx, i);
		}

		async_tx_submit(chan, tx, flags, depend_tx, callback,
			callback_param);
	} else {
		unsigned long lflags = flags;

		lflags &= ~ASYNC_TX_ACK;
		lflags |= ASYNC_TX_XOR_ZERO_DST;

		tx = async_pqxor(pdest, qdest,
			src_list, scoef_list,
			offset, src_cnt, len, lflags,
			depend_tx, NULL, NULL);

		if (tx) {
			if (dma_wait_for_async_tx(tx) == DMA_ERROR)
				panic("%s: DMA_ERROR waiting for tx\n",
					__FUNCTION__);
			async_tx_ack(tx);
		}

		if (presult && pdest)
			*presult = page_is_zero(pdest, len) ? 0 : 1;
		if (qresult && qdest)
			*qresult = page_is_zero (qdest, len) ? 0 : 1;

		tx = NULL;

		async_tx_sync_epilog(flags, depend_tx, callback, callback_param);
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_pqxor_zero_sum);

static int __init async_pqxor_init(void)
{
	return 0;
}

static void __exit async_pqxor_exit(void)
{
	do { } while (0);
}

module_init(async_pqxor_init);
module_exit(async_pqxor_exit);

MODULE_AUTHOR("Yuri Tikhonov <yur@emcraft.com>");
MODULE_DESCRIPTION("asynchronous qxor/qxor-zero-sum api");
MODULE_LICENSE("GPL");
