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
 *  The following static variables are used in cases of synchronous
 * zero sum to save the values to check. Two pages used for zero sum and
 * the third one is for dumb P destination when calling gen_syndrome()
 */
static spinlock_t spare_lock;
static struct page *spare_pages[3];

/**
 * do_async_pq - asynchronously calculate P and/or Q
 */
static struct dma_async_tx_descriptor *
do_async_pq(struct dma_chan *chan, struct page **blocks, unsigned char *scfs,
	unsigned int offset, int src_cnt, size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	struct dma_device *dma = chan->device;
	dma_addr_t dma_dest[2], dma_src[src_cnt];
	struct dma_async_tx_descriptor *tx = NULL;
	dma_async_tx_callback _cb_fn;
	void *_cb_param;
	unsigned char *scf = NULL;
	int i, src_off = 0;
	unsigned short pq_src_cnt;
	enum async_tx_flags async_flags;
	enum dma_ctrl_flags dma_flags = 0;

	/*  If we won't handle src_cnt in one shot, then the following
	 * flag(s) will be set only on the first pass of prep_dma
	 */
	if (flags & ASYNC_TX_PQ_ZERO_P)
		dma_flags |= DMA_PREP_ZERO_P;
	if (flags & ASYNC_TX_PQ_ZERO_Q)
		dma_flags |= DMA_PREP_ZERO_Q;

	/* DMAs use destinations as sources, so use BIDIRECTIONAL mapping */
	if (blocks[src_cnt]) {
		dma_dest[0] = dma_map_page(dma->dev, blocks[src_cnt],
					   offset, len, DMA_BIDIRECTIONAL);
		dma_flags |= DMA_PREP_HAVE_P;
	}
	if (blocks[src_cnt+1]) {
		dma_dest[1] = dma_map_page(dma->dev, blocks[src_cnt+1],
					   offset, len, DMA_BIDIRECTIONAL);
		dma_flags |= DMA_PREP_HAVE_Q;
	}

	for (i = 0; i < src_cnt; i++)
		dma_src[i] = dma_map_page(dma->dev, blocks[i],
					  offset, len, DMA_TO_DEVICE);

	while (src_cnt) {
		async_flags = flags;
		pq_src_cnt = min(src_cnt, (int)dma->max_pq);
		/* if we are submitting additional pqs, leave the chain open,
		 * clear the callback parameters, and leave the destination
		 * buffers mapped
		 */
		if (src_cnt > pq_src_cnt) {
			async_flags &= ~ASYNC_TX_ACK;
			dma_flags |= DMA_COMPL_SKIP_DEST_UNMAP;
			_cb_fn = NULL;
			_cb_param = NULL;
		} else {
			_cb_fn = cb_fn;
			_cb_param = cb_param;
		}
		if (_cb_fn)
			dma_flags |= DMA_PREP_INTERRUPT;
		if (scfs)
			scf = &scfs[src_off];

		/* Since we have clobbered the src_list we are committed
		 * to doing this asynchronously.  Drivers force forward
		 * progress in case they can not provide a descriptor
		 */
		tx = dma->device_prep_dma_pq(chan, dma_dest,
					     &dma_src[src_off], pq_src_cnt,
					     scf, len, dma_flags);
		if (unlikely(!tx))
			async_tx_quiesce(&depend_tx);

		/* spin wait for the preceeding transactions to complete */
		while (unlikely(!tx)) {
			dma_async_issue_pending(chan);
			tx = dma->device_prep_dma_pq(chan, dma_dest,
					&dma_src[src_off], pq_src_cnt,
					scf, len, dma_flags);
		}

		async_tx_submit(chan, tx, async_flags, depend_tx,
				_cb_fn, _cb_param);

		depend_tx = tx;
		flags |= ASYNC_TX_DEP_ACK;

		if (src_cnt > pq_src_cnt) {
			/* drop completed sources */
			src_cnt -= pq_src_cnt;
			src_off += pq_src_cnt;

			/* use the intermediate result as a source; we
			 * clear DMA_PREP_ZERO, so prep_dma_pq will
			 * include destination(s) into calculations. Thus
			 * keep DMA_PREP_HAVE_x in dma_flags only
			 */
			dma_flags &= (DMA_PREP_HAVE_P | DMA_PREP_HAVE_Q);
		} else
			break;
	}

	return tx;
}

/**
 * do_sync_pq - synchronously calculate P and Q
 */
static void
do_sync_pq(struct page **blocks, unsigned char *scfs, unsigned int offset,
	int src_cnt, size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	int i, pos;
	uint8_t *p = NULL, *q = NULL, *src;

	/* set destination addresses */
	if (blocks[src_cnt])
		p = (uint8_t *)(page_address(blocks[src_cnt]) + offset);
	if (blocks[src_cnt+1])
		q = (uint8_t *)(page_address(blocks[src_cnt+1]) + offset);

	if (flags & ASYNC_TX_PQ_ZERO_P) {
		BUG_ON(!p);
		memset(p, 0, len);
	}

	if (flags & ASYNC_TX_PQ_ZERO_Q) {
		BUG_ON(!q);
		memset(q, 0, len);
	}

	for (i = 0; i < src_cnt; i++) {
		src = (uint8_t *)(page_address(blocks[i]) + offset);
		for (pos = 0; pos < len; pos++) {
			if (p)
				p[pos] ^= src[pos];
			if (q)
				q[pos] ^= raid6_gfmul[scfs[i]][src[pos]];
		}
	}
	async_tx_sync_epilog(cb_fn, cb_param);
}

/**
 * async_pq - attempt to do XOR and Galois calculations in parallel using
 *	a dma engine.
 * @blocks: source block array from 0 to (src_cnt-1) with the p destination
 *	at blocks[src_cnt] and q at blocks[src_cnt + 1]. Only one of two
 *	destinations may be present (another then has to be set to NULL).
 *	By default, the result of calculations is XOR-ed with the initial
 *	content of the destinationa buffers. Use ASYNC_TX_PQ_ZERO_x flags
 *	to avoid this.
 *	NOTE: client code must assume the contents of this array are destroyed
 * @scfs: array of source coefficients used in GF-multiplication
 * @offset: offset in pages to start transaction
 * @src_cnt: number of source pages
 * @len: length in bytes
 * @flags: ASYNC_TX_PQ_ZERO_P, ASYNC_TX_PQ_ZERO_Q, ASYNC_TX_ASSUME_COHERENT,
 *	ASYNC_TX_ACK, ASYNC_TX_DEP_ACK, ASYNC_TX_ASYNC_ONLY
 * @depend_tx: depends on the result of this transaction.
 * @cb_fn: function to call when the operation completes
 * @cb_param: parameter to pass to the callback routine
 */
struct dma_async_tx_descriptor *
async_pq(struct page **blocks, unsigned char *scfs, unsigned int offset,
	int src_cnt, size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	struct dma_chan *chan = async_tx_find_channel(depend_tx, DMA_PQ,
					&blocks[src_cnt], 2,
					blocks, src_cnt, len);
	struct dma_device *device = chan ? chan->device : NULL;
	struct dma_async_tx_descriptor *tx = NULL;

	if (!device && (flags & ASYNC_TX_ASYNC_ONLY))
		return NULL;

	if (device) {
		/* run pq asynchronously */
		tx = do_async_pq(chan, blocks, scfs, offset, src_cnt,
			len, flags, depend_tx, cb_fn,cb_param);
	} else {
		/* run pq synchronously */
		if (!blocks[src_cnt+1]) {
			struct page *pdst = blocks[src_cnt];
			int i;

			/* Calculate P-parity only.
			 * As opposite to async_xor(), async_pq() assumes
			 * that destinations are included into calculations,
			 * so we should re-arrange the xor src list to
			 * achieve the similar behavior.
			 */
			if (!(flags & ASYNC_TX_PQ_ZERO_P)) {
				/* If async_pq() user doesn't set ZERO flag,
				 * it's assumed that destination has some
				 * reasonable data to include in calculations.
				 * The destination must be at position 0, so
				 * shift the sources and put pdst at the
				 * beginning of the list.
				 */
				for (i = src_cnt - 1; i >= 0; i--)
					blocks[i+1] = blocks[i];
				blocks[0] = pdst;
				src_cnt++;
				flags |= ASYNC_TX_XOR_DROP_DST;
			} else {
				/* If async_pq() user want to clear P, then
				 * this will be done automatically in async
				 * case, and with the help of ZERO_DST in
				 * the sync one.
				 */
				flags &= ~ASYNC_TX_PQ_ZERO_P;
				flags |= ASYNC_TX_XOR_ZERO_DST;
			}

			return async_xor(pdst, blocks, offset,
					 src_cnt, len, flags, depend_tx,
					 cb_fn, cb_param);
		}

		/* wait for any prerequisite operations */
		async_tx_quiesce(&depend_tx);

		do_sync_pq(blocks, scfs, offset, src_cnt, len, flags,
			depend_tx, cb_fn, cb_param);
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_pq);

/**
 * do_sync_gen_syndrome - synchronously calculate P (xor) and Q (Reed-Solomon
 *	code)
 */
static void
do_sync_gen_syndrome(struct page **blocks, unsigned int offset, int src_cnt,
	size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	int i;
	void *tsrc[src_cnt+2];

	for (i = 0; i < src_cnt + 2; i++)
		tsrc[i] = page_address(blocks[i]) + offset;

	raid6_call.gen_syndrome(i, len, tsrc);

	async_tx_sync_epilog(cb_fn, cb_param);
}

/**
 * async_gen_syndrome - attempt to generate P (xor) and Q (Reed-Solomon code)
 *	with a dma engine for a given set of blocks.  This routine assumes a
 *	field of GF(2^8) with a primitive polynomial of 0x11d and a generator
 *	of {02}.
 * @blocks: source block array ordered from 0..src_cnt-1 with the P destination
 *	at blocks[src_cnt] and Q at blocks[src_cnt + 1]. Only one of two
 *	destinations may be present (another then has to be set to NULL).
 *	NOTE: client code must assume the contents of this array are destroyed
 * @offset: offset in pages to start transaction
 * @src_cnt: number of source pages: 2 < src_cnt <= 255
 * @len: length of blocks in bytes
 * @flags: ASYNC_TX_ACK, ASYNC_TX_DEP_ACK, ASYNC_TX_ASYNC_ONLY
 * @depend_tx: P+Q operation depends on the result of this transaction.
 * @cb_fn: function to call when P+Q generation completes
 * @cb_param: parameter to pass to the callback routine
 */
struct dma_async_tx_descriptor *
async_gen_syndrome(struct page **blocks, unsigned int offset, int src_cnt,
	size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	struct dma_chan *chan = async_tx_find_channel(depend_tx, DMA_PQ,
						     &blocks[src_cnt], 2,
						     blocks, src_cnt, len);
	struct dma_device *device = chan ? chan->device : NULL;
	struct dma_async_tx_descriptor *tx = NULL;

	BUG_ON(src_cnt > 255 || (!blocks[src_cnt] && !blocks[src_cnt+1]));

	if (!device && (flags & ASYNC_TX_ASYNC_ONLY))
		return NULL;

	/* Synchronous gen_syndrome() doesn't take care of destinations,
	 * but asynchronous implies them as sources; so, when generating
	 * syndromes - command to clear destinations up explicitly
	 */
	if (blocks[src_cnt])
		flags |= ASYNC_TX_PQ_ZERO_P;
	if (blocks[src_cnt+1])
		flags |= ASYNC_TX_PQ_ZERO_Q;

	if (device) {
		/* run the xor asynchronously */
		tx = do_async_pq(chan, blocks, (uint8_t *)raid6_gfexp,
				 offset, src_cnt, len, flags, depend_tx,
				 cb_fn, cb_param);
	} else {
		/* run the pq synchronously */
		/* wait for any prerequisite operations */
		async_tx_quiesce(&depend_tx);

		if (!blocks[src_cnt])
			blocks[src_cnt] = spare_pages[2];
		if (!blocks[src_cnt+1])
			blocks[src_cnt+1] = spare_pages[2];
		do_sync_gen_syndrome(blocks, offset, src_cnt, len, flags,
				     depend_tx, cb_fn, cb_param);
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_gen_syndrome);

/**
 * async_pq_zero_sum - attempt a PQ parities check with a dma engine.
 * @blocks: array of source pages. The 0..src_cnt-1 are the sources, the
 *	src_cnt and src_cnt+1 are the P and Q destinations to check, resp.
 *	Only one of two destinations may be present.
 *	NOTE: client code must assume the contents of this array are destroyed
 * @scfs: coefficients to use in GF-multiplications
 * @offset: offset in pages to start transaction
 * @src_cnt: number of source pages
 * @len: length in bytes
 * @presult: where to store the result of P-ckeck, which is 0 if P-parity
 *	OK, and non-zero otherwise.
 * @qresult: where to store the result of Q-ckeck, which is 0 if Q-parity
 *	OK, and non-zero otherwise.
 * @flags: ASYNC_TX_ASSUME_COHERENT, ASYNC_TX_ACK, ASYNC_TX_DEP_ACK
 * @depend_tx: depends on the result of this transaction.
 * @cb_fn: function to call when the xor completes
 * @cb_param: parameter to pass to the callback routine
 */
struct dma_async_tx_descriptor *
async_pq_zero_sum(struct page **blocks, unsigned char *scfs,
	unsigned int offset, int src_cnt, size_t len, u32 *pqres,
	enum async_tx_flags flags, struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	struct dma_chan *chan = async_tx_find_channel(depend_tx,
						      DMA_PQ_ZERO_SUM,
						      &blocks[src_cnt], 2,
						      blocks, src_cnt, len);
	struct dma_device *device = chan ? chan->device : NULL;
	struct dma_async_tx_descriptor *tx = NULL;
	enum dma_ctrl_flags dma_flags = cb_fn ? DMA_PREP_INTERRUPT : 0;

	BUG_ON(src_cnt < 2);

	if (blocks[src_cnt])
		dma_flags |= DMA_PREP_HAVE_P;
	if (blocks[src_cnt+1])
		dma_flags |= DMA_PREP_HAVE_Q;

	if (device && src_cnt <= (int)device->max_pq) {
		dma_addr_t dma_src[src_cnt + 2];
		int i;

		for (i = 0; i < src_cnt + 2; i++) {
			if (likely(blocks[i])) {
				dma_src[i] = dma_map_page(device->dev,
							  blocks[i], offset,
							  len, DMA_TO_DEVICE);
			}
		}

		tx = device->device_prep_dma_pqzero_sum(chan, dma_src, src_cnt,
							scfs, len, pqres,
							dma_flags);

		if (unlikely(!tx)) {
			async_tx_quiesce(&depend_tx);

			while (unlikely(!tx)) {
				dma_async_issue_pending(chan);
				tx = device->device_prep_dma_pqzero_sum(chan,
						dma_src, src_cnt, scfs, len,
						pqres, dma_flags);
			}
		}

		async_tx_submit(chan, tx, flags, depend_tx, cb_fn, cb_param);
	} else {
		struct page *pdest = blocks[src_cnt];
		struct page *qdest = blocks[src_cnt + 1];
		enum async_tx_flags lflags = flags;

		lflags &= ~ASYNC_TX_ACK;
		lflags |= ASYNC_TX_PQ_ZERO_P | ASYNC_TX_PQ_ZERO_Q;

		spin_lock(&spare_lock);
		blocks[src_cnt] = spare_pages[0];
		blocks[src_cnt + 1] = spare_pages[1];
		tx = async_pq(blocks, scfs, offset, src_cnt, len, lflags,
			      depend_tx, NULL, NULL);
		async_tx_quiesce(&tx);

		if (dma_flags & DMA_PREP_HAVE_P) {
			if (memcmp(page_address(pdest) + offset,
				   page_address(spare_pages[0]) + offset,
				   len) == 0)
				*pqres &= ~DMA_PCHECK_FAILED;
			else
				*pqres |= DMA_PCHECK_FAILED;
		}
		if (dma_flags & DMA_PREP_HAVE_Q) {
			if (memcmp(page_address(qdest) + offset,
				   page_address(spare_pages[1]) + offset,
				   len) == 0)
				*pqres &= ~DMA_QCHECK_FAILED;
			else
				*pqres |= DMA_QCHECK_FAILED;
		}
		spin_unlock(&spare_lock);
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_pq_zero_sum);

/**
 * async_syndrome_zero_sum - attempt a P (xor) and Q (Reed-Solomon code)
 *	parities check with a dma engine. This routine assumes a field of
 *	GF(2^8) with a primitive polynomial of 0x11d and a generator of {02}.
 * @blocks: array of source pages. The 0..src_cnt-1 are the sources, the
 *	src_cnt and src_cnt+1 are the P and Q destinations to check, resp.
 *	Only one of two destinations may be present.
 *	NOTE: client code must assume the contents of this array are destroyed
 * @offset: offset in pages to start transaction
 * @src_cnt: number of source pages
 * @len: length in bytes
 * @pqres: the pointer, where to flag about the result of the check: the
 * 	result of P-check is stored at bit0, the result of Q-check is stored
 * 	at bit1. If the bit is cleared, then the corresponding parity is OK.
 * 	If the bit is set, then the corresponding parity is bad.
 * @flags: ASYNC_TX_ASSUME_COHERENT, ASYNC_TX_ACK, ASYNC_TX_DEP_ACK
 * @depend_tx: depends on the result of this transaction.
 * @cb_fn: function to call when the xor completes
 * @cb_param: parameter to pass to the callback routine
 */
struct dma_async_tx_descriptor *
async_syndrome_zero_sum(struct page **blocks, unsigned int offset,
	int src_cnt, size_t len, u32 *pqres, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	struct dma_chan *chan = async_tx_find_channel(depend_tx,
						      DMA_PQ_ZERO_SUM,
						      &blocks[src_cnt], 2,
						      blocks, src_cnt, len);
	struct dma_device *device = chan ? chan->device : NULL;
	struct dma_async_tx_descriptor *tx = NULL;
	enum dma_ctrl_flags dma_flags = cb_fn ? DMA_PREP_INTERRUPT : 0;

	BUG_ON(src_cnt < 2);

	if (blocks[src_cnt])
		dma_flags |= DMA_PREP_HAVE_P;
	if (blocks[src_cnt+1])
		dma_flags |= DMA_PREP_HAVE_Q;

	if (device && src_cnt <= (int)device->max_pq) {
		dma_addr_t dma_src[src_cnt + 2];
		int i;

		
		for (i = 0; i < src_cnt + 2; i++) {
			if (likely(blocks[i])) {
				dma_src[i] = dma_map_page(device->dev,
							  blocks[i], offset,
							  len, DMA_TO_DEVICE);
			}
		}

		tx = device->device_prep_dma_pqzero_sum(chan, dma_src, src_cnt,
							(uint8_t *)raid6_gfexp,
							len, pqres, dma_flags);

		if (unlikely(!tx)) {
			async_tx_quiesce(&depend_tx);
			while (unlikely(!tx)) {
				dma_async_issue_pending(chan);
				tx = device->device_prep_dma_pqzero_sum(chan,
						dma_src, src_cnt,
						(uint8_t *)raid6_gfexp, len,
						pqres, dma_flags);
			}
		}

		async_tx_submit(chan, tx, flags, depend_tx, cb_fn, cb_param);
	} else {
		struct page *pdest = blocks[src_cnt];
		struct page *qdest = blocks[src_cnt + 1];
		enum async_tx_flags lflags = flags;

		lflags &= ~ASYNC_TX_ACK;

		spin_lock(&spare_lock);
		blocks[src_cnt] = spare_pages[0];
		blocks[src_cnt + 1] = spare_pages[1];
		tx = async_gen_syndrome(blocks, offset,
					src_cnt, len, lflags,
					depend_tx, NULL, NULL);
		async_tx_quiesce(&tx);

		if (dma_flags & DMA_PREP_HAVE_P) {
			if (memcmp(page_address(pdest) + offset,
				   page_address(spare_pages[0]) + offset,
				   len) == 0)
				*pqres &= ~DMA_PCHECK_FAILED;
			else
				*pqres |= DMA_PCHECK_FAILED;
		}
		if (dma_flags & DMA_PREP_HAVE_Q) {
			if (memcmp(page_address(qdest) + offset,
				   page_address(spare_pages[1]) + offset,
				   len) == 0)
				*pqres &= ~DMA_QCHECK_FAILED;
			else
				*pqres |= DMA_QCHECK_FAILED;
		}
		spin_unlock(&spare_lock);
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_syndrome_zero_sum);

static int __init async_pq_init(void)
{
	spin_lock_init(&spare_lock);

	spare_pages[0] = alloc_page(GFP_KERNEL);
	if (!spare_pages[0])
		goto abort;
	spare_pages[1] = alloc_page(GFP_KERNEL);
	if (!spare_pages[1])
		goto abort;
	spare_pages[2] = alloc_page(GFP_KERNEL);
	if (!spare_pages[2])
		goto abort;
	return 0;
abort:
	safe_put_page(spare_pages[2]);
	safe_put_page(spare_pages[1]);
	safe_put_page(spare_pages[0]);
	printk(KERN_ERR "%s: cannot allocate spare!\n", __func__);
	return -ENOMEM;
}

static void __exit async_pq_exit(void)
{
	safe_put_page(spare_pages[2]);
	safe_put_page(spare_pages[1]);
	safe_put_page(spare_pages[0]);
}

module_init(async_pq_init);
module_exit(async_pq_exit);

MODULE_AUTHOR("Yuri Tikhonov <yur@emcraft.com>");
MODULE_DESCRIPTION("asynchronous pq/pq-zero-sum api");
MODULE_LICENSE("GPL");
