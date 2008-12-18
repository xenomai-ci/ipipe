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
struct page *spare_pages[3];

/**
 * do_async_pqxor - asynchronously calculate P and/or Q
 */
static struct dma_async_tx_descriptor *
do_async_pqxor(struct dma_chan *chan, struct page *pdest, struct page *qdest,
	struct page **src_list, unsigned char *scoef_list,
	unsigned int offset, unsigned int src_cnt, size_t len,
	enum async_tx_flags flags, struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	struct dma_device *dma = chan->device;
	struct page *dest;
	dma_addr_t dma_dest[2];
	dma_addr_t *dma_src = (dma_addr_t *) src_list;
	unsigned char *scf = qdest ? scoef_list : NULL;
	struct dma_async_tx_descriptor *tx;
	int i, dst_cnt = 0;
	unsigned long dma_prep_flags = cb_fn ? DMA_PREP_INTERRUPT : 0;

	if (flags & ASYNC_TX_XOR_ZERO_DST)
		dma_prep_flags |= DMA_PREP_ZERO_DST;

	/*  One parity (P or Q) calculation is initiated always;
	 * first always try Q
	 */
	dest = qdest ? qdest : pdest;
	dma_dest[dst_cnt++] = dma_map_page(dma->dev, dest, offset, len,
					    DMA_FROM_DEVICE);

	/* Switch to the next destination */
	if (qdest && pdest) {
		/* Both destinations are set, thus here we deal with P */
		dma_dest[dst_cnt++] = dma_map_page(dma->dev, pdest, offset,
						len, DMA_FROM_DEVICE);
	}

	for (i = 0; i < src_cnt; i++)
		dma_src[i] = dma_map_page(dma->dev, src_list[i],
			offset, len, DMA_TO_DEVICE);

	/* Since we have clobbered the src_list we are committed
	 * to doing this asynchronously.  Drivers force forward progress
	 * in case they can not provide a descriptor
	 */
	tx = dma->device_prep_dma_pqxor(chan, dma_dest, dst_cnt, dma_src,
					   src_cnt, scf, len, dma_prep_flags);
	if (!tx) {
		if (depend_tx)
			dma_wait_for_async_tx(depend_tx);

		while (!tx)
			tx = dma->device_prep_dma_pqxor(chan,
							   dma_dest, dst_cnt,
							   dma_src, src_cnt,
							   scf, len,
							   dma_prep_flags);
	}

	async_tx_submit(chan, tx, flags, depend_tx, cb_fn, cb_param);

	return tx;
}

/**
 * do_sync_pqxor - synchronously calculate P and Q
 */
static void
do_sync_pqxor(struct page *pdest, struct page *qdest,
	struct page **src_list, unsigned char *scoef_list, unsigned int offset,
	unsigned int src_cnt, size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	int i, pos;
	uint8_t *p, *q, *src;

	/* set destination addresses */
	p = pdest ? (uint8_t *)(page_address(pdest) + offset) : NULL;
	q = (uint8_t *)(page_address(qdest) + offset);

	if (flags & ASYNC_TX_XOR_ZERO_DST) {
		if (p)
			memset(p, 0, len);
		memset(q, 0, len);
	}

	for (i = 0; i < src_cnt; i++) {
		src = (uint8_t *)(page_address(src_list[i]) + offset);
		for (pos = 0; pos < len; pos++) {
			if (p)
				p[pos] ^= src[pos];
			q[pos] ^= raid6_gfmul[scoef_list[i]][src[pos]];
		}
	}
	async_tx_sync_epilog(cb_fn, cb_param);
}

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
	struct dma_chan *chan = async_tx_find_channel(depend_tx, DMA_PQ_XOR,
						      dest, 2, src_list,
						      src_cnt, len);
	struct dma_device *device = chan ? chan->device : NULL;
	struct dma_async_tx_descriptor *tx = NULL;

	BUG_ON(!pdest && !qdest);

	if (!device && (flags & ASYNC_TX_ASYNC_ONLY))
		return NULL;

	if (device) { /* run the xor asynchronously */
		tx = do_async_pqxor(chan, pdest, qdest, src_list,
			       scoef_list, offset, src_cnt, len, flags,
			       depend_tx, callback,callback_param);
	} else { /* run the pqxor synchronously */
		if (!qdest) {
			struct page *tsrc[src_cnt + 1];
			struct page **lsrc = src_list;
			if (!(flags & ASYNC_TX_XOR_ZERO_DST)) {
				tsrc[0] = pdest;
				memcpy(tsrc + 1, src_list, src_cnt *
						sizeof(struct page *));
				lsrc = tsrc;
				src_cnt++;
				flags |= ASYNC_TX_XOR_DROP_DST;
			}
			return async_xor(pdest, lsrc, offset, src_cnt, len,
					flags, depend_tx,
					callback, callback_param);
		}

		/* wait for any prerequisite operations */
		async_tx_quiesce(&depend_tx);

		do_sync_pqxor(pdest, qdest, src_list, scoef_list,
			offset,	src_cnt, len, flags, depend_tx,
			callback, callback_param);
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_pqxor);

/**
 * do_sync_gen_syndrome - synchronously calculate P and Q
 */
static void
do_sync_gen_syndrome(struct page *pdest, struct page *qdest,
	struct page **src_list, unsigned int offset,
	unsigned int src_cnt, size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback callback, void *callback_param)
{
	int i;
	void *tsrc[src_cnt + 2];

	for (i = 0; i < src_cnt; i++)
		tsrc[i] = page_address(src_list[i]) + offset;

	/* set destination addresses */
	tsrc[i++] = page_address(pdest) + offset;
	tsrc[i++] = page_address(qdest) + offset;

	if (flags & ASYNC_TX_XOR_ZERO_DST) {
		memset(tsrc[i-2], 0, len);
		memset(tsrc[i-1], 0, len);
	}

	raid6_call.gen_syndrome(i, len, tsrc);
	async_tx_sync_epilog(callback, callback_param);
}

/**
 * async_gen_syndrome - attempt to calculate RS-syndrome and XOR in parallel
 * using a dma engine.
 * @pdest: destination page for P-parity (XOR)
 * @qdest: destination page for Q-parity (GF-XOR)
 * @src_list: array of source pages
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
async_gen_syndrome(struct page *pdest, struct page *qdest,
	struct page **src_list,	unsigned int offset, int src_cnt, size_t len,
	enum async_tx_flags flags, struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback callback, void *callback_param)
{
	struct dma_chan *chan = async_tx_find_channel(depend_tx, DMA_PQ_XOR,
						      dest, 2, src_list,
						      src_cnt, len);
	struct dma_device *device = chan ? chan->device : NULL;
	struct dma_async_tx_descriptor *tx = NULL;

	if (!device && (flags & ASYNC_TX_ASYNC_ONLY))
		return NULL;

	if (device) { /* run the xor asynchronously */
		tx = do_async_pqxor(chan, pdest, qdest, src_list,
			       (uint8_t *)raid6_gfexp, offset, src_cnt,
			       len, flags, depend_tx, callback, callback_param);
	} else { /* run the pqxor synchronously */
		if (!qdest) {
			struct page *tsrc[src_cnt + 1];
			struct page **lsrc = src_list;
			if (!(flags & ASYNC_TX_XOR_ZERO_DST)) {
				tsrc[0] = pdest;
				memcpy(tsrc + 1, src_list, src_cnt *
						sizeof(struct page *));
				lsrc = tsrc;
				src_cnt++;
				flags |= ASYNC_TX_XOR_DROP_DST;
			}
			return async_xor(pdest, lsrc, offset, src_cnt, len,
					flags, depend_tx,
					callback, callback_param);
		}

		/* may do synchronous PQ only when both destinations exsists */
		if (!pdest)
			pdest = spare_pages[2];

		/* wait for any prerequisite operations */
		if (depend_tx) {
			/* if ack is already set then we cannot be sure
			 * we are referring to the correct operation
			 */
			BUG_ON(async_tx_test_ack(depend_tx));
			if (dma_wait_for_async_tx(depend_tx) == DMA_ERROR)
				panic("%s: DMA_ERROR waiting for depend_tx\n",
					__func__);
		}

		do_sync_gen_syndrome(pdest, qdest, src_list,
			offset,	src_cnt, len, flags, depend_tx,
			callback, callback_param);
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_gen_syndrome);

/**
 * async_pqxor_zero_sum - attempt a PQ parities check with a dma engine.
 * @pdest: P-parity destination to check
 * @qdest: Q-parity destination to check
 * @src_list: array of source pages; the 1st pointer is qdest, the 2nd - pdest.
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
	struct page **src_list, unsigned char *scf,
	unsigned int offset, int src_cnt, size_t len,
	u32 *presult, u32 *qresult, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	struct dma_chan *chan = async_tx_find_channel(depend_tx,
						      DMA_PQ_ZERO_SUM,
						      src_list, 2, &src_list[2],
						      src_cnt, len);
	struct dma_device *device = chan ? chan->device : NULL;
	struct dma_async_tx_descriptor *tx = NULL;

	BUG_ON(src_cnt <= 1);
	BUG_ON(!qdest || qdest != src_list[0] || pdest != src_list[1]);

	if (device) {
		dma_addr_t *dma_src = (dma_addr_t *)src_list;
		unsigned long dma_prep_flags = cb_fn ? DMA_PREP_INTERRUPT : 0;
		int i;

		for (i = 0; i < src_cnt; i++)
			dma_src[i] = src_list[i] ? dma_map_page(device->dev,
					src_list[i], offset, len,
					DMA_TO_DEVICE) : 0;

		tx = device->device_prep_dma_pqzero_sum(chan, dma_src, src_cnt,
						      scf, len,
						      presult, qresult,
						      dma_prep_flags);

		if (unlikely(!tx)) {
			async_tx_quiesce(&depend_tx);

			while (!tx) {
				dma_async_issue_pending(chan);
				tx = device->device_prep_dma_pqzero_sum(chan,
						dma_src, src_cnt, scf, len,
						presult, qresult,
						dma_prep_flags);
			}
		}

		async_tx_submit(chan, tx, flags, depend_tx, cb_fn, cb_param);
	} else {
		unsigned long lflags = flags;

		/* TBD: support for lengths size of more than PAGE_SIZE */

		lflags &= ~ASYNC_TX_ACK;
		lflags |= ASYNC_TX_XOR_ZERO_DST;
		spin_lock(&spare_lock);
		do_sync_pqxor(spare_pages[0], spare_pages[1],
			&src_list[2], scf, offset,
			src_cnt - 2, len, lflags,
			depend_tx, NULL, NULL);

		if (presult && pdest)
			*presult = memcmp(page_address(pdest) + offset,
					   page_address(spare_pages[0]) +
					   offset, len) == 0 ? 0 : 1;
		if (qresult && qdest)
			*qresult = memcmp(page_address(qdest) + offset,
					   page_address(spare_pages[1]) +
					   offset, len) == 0 ? 0 : 1;
		spin_unlock(&spare_lock);
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_pqxor_zero_sum);

/**
 * async_syndrome_zero_sum - attempt a PQ parities check with a dma engine.
 * @pdest: P-parity destination to check
 * @qdest: Q-parity destination to check
 * @src_list: array of source pages; the 1st pointer is qdest, the 2nd - pdest.
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
async_syndrome_zero_sum(struct page *pdest, struct page *qdest,
	struct page **src_list, unsigned int offset, int src_cnt, size_t len,
	u32 *presult, u32 *qresult, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	struct dma_chan *chan = async_tx_find_channel(depend_tx,
						      DMA_PQ_ZERO_SUM,
						      src_list, 2, &src_list[2],
						      src_cnt, len);
	struct dma_device *device = chan ? chan->device : NULL;
	struct dma_async_tx_descriptor *tx = NULL;

	BUG_ON(src_cnt <= 1);
	BUG_ON(!qdest || qdest != src_list[0] || pdest != src_list[1]);

	if (device) {
		dma_addr_t *dma_src = (dma_addr_t *)src_list;
		unsigned long dma_prep_flags = cb_fn ? DMA_PREP_INTERRUPT : 0;
		int i;

		for (i = 0; i < src_cnt; i++)
			dma_src[i] = src_list[i] ? dma_map_page(device->dev,
					src_list[i], offset, len,
					DMA_TO_DEVICE) : 0;

		tx = device->device_prep_dma_pqzero_sum(chan, dma_src, src_cnt,
						      (uint8_t *)raid6_gfexp,
						      len, presult, qresult,
						      dma_prep_flags);

		if (!tx) {
			if (depend_tx)
				dma_wait_for_async_tx(depend_tx);

			while (!tx)
				tx = device->device_prep_dma_pqzero_sum(chan,
						dma_src, src_cnt,
						(uint8_t *)raid6_gfexp, len,
						presult, qresult,
						dma_prep_flags);
		}

		async_tx_submit(chan, tx, flags, depend_tx, cb_fn, cb_param);
	} else {
		unsigned long lflags = flags;

		/* TBD: support for lengths size of more than PAGE_SIZE */

		lflags &= ~ASYNC_TX_ACK;
		lflags |= ASYNC_TX_XOR_ZERO_DST;
		spin_lock(&spare_lock);
		do_sync_gen_syndrome(spare_pages[0], spare_pages[1],
			&src_list[2], offset,
			src_cnt - 2, len, lflags,
			depend_tx, NULL, NULL);

		if (presult && pdest)
			*presult = memcmp(page_address(pdest) + offset,
					   page_address(spare_pages[0]) +
					   offset, len) == 0 ? 0 : 1;
		if (qresult && qdest)
			*qresult = memcmp(page_address(qdest) + offset,
					   page_address(spare_pages[1]) +
					   offset, len) == 0 ? 0 : 1;
		spin_unlock(&spare_lock);
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_syndrome_zero_sum);

static int __init async_pqxor_init(void)
{
	spin_lock_init(&spare_lock);

	spare_pages[0] = alloc_page(GFP_KERNEL);
	if (!spare_pages[0])
		goto abort;
	spare_pages[1] = alloc_page(GFP_KERNEL);
	if (!spare_pages[1])
		goto abort;
	spare_pages[2] = alloc_page(GFP_KERNEL);

	return 0;
abort:
	safe_put_page(spare_pages[0]);
	safe_put_page(spare_pages[1]);
	printk(KERN_ERR "%s: cannot allocate spare!\n", __func__);
	return -ENOMEM;
}

static void __exit async_pqxor_exit(void)
{
	safe_put_page(spare_pages[0]);
	safe_put_page(spare_pages[1]);
	safe_put_page(spare_pages[2]);
}

module_init(async_pqxor_init);
module_exit(async_pqxor_exit);

MODULE_AUTHOR("Yuri Tikhonov <yur@emcraft.com>");
MODULE_DESCRIPTION("asynchronous qxor/qxor-zero-sum api");
MODULE_LICENSE("GPL");
