/*
 * Copyright(c) 2006 Intel Corporation. All rights reserved.
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
#ifndef _ASYNC_TX_H_
#define _ASYNC_TX_H_
#include <linux/dmaengine.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>

/**
 * dma_chan_ref - object used to manage dma channels received from the
 *   dmaengine core.
 * @chan - the channel being tracked
 * @node - node for the channel to be placed on async_tx_master_list
 * @rcu - for list_del_rcu
 * @count - number of times this channel is listed in the pool
 *	(for channels with multiple capabiities)
 */
struct dma_chan_ref {
	struct dma_chan *chan;
	struct list_head node;
	struct rcu_head rcu;
	atomic_t count;
};

/**
 * async_tx_flags - modifiers for the async_* calls
 * @ASYNC_TX_XOR_ZERO_DST: this flag must be used for xor operations where the
 * the destination address is not a source.  The asynchronous case handles this
 * implicitly, the synchronous case needs to zero the destination block.
 * @ASYNC_TX_XOR_DROP_DST: this flag must be used if the destination address is
 * also one of the source addresses.  In the synchronous case the destination
 * address is an implied source, whereas the asynchronous case it must be listed
 * as a source.  The destination address must be the first address in the source
 * array.
 * @ASYNC_TX_ASSUME_COHERENT: skip cache maintenance operations
 * @ASYNC_TX_ACK: immediately ack the descriptor, precludes setting up a
 * dependency chain
 * @ASYNC_TX_DEP_ACK: ack the dependency descriptor.  Useful for chaining.
 * @ASYNC_TX_KMAP_SRC: if the transaction is to be performed synchronously
 * take an atomic mapping (KM_USER0) on the source page(s)
 * @ASYNC_TX_KMAP_DST: if the transaction is to be performed synchronously
 * take an atomic mapping (KM_USER0) on the dest page(s)
 * @ASYNC_TX_ASYNC_ONLY: if set then try to perform operation requested in
 * asynchronous way only.
 */
enum async_tx_flags {
	ASYNC_TX_XOR_ZERO_DST	 = (1 << 0),
	ASYNC_TX_XOR_DROP_DST	 = (1 << 1),
	ASYNC_TX_ASSUME_COHERENT = (1 << 2),
	ASYNC_TX_ACK		 = (1 << 3),
	ASYNC_TX_DEP_ACK	 = (1 << 4),
	ASYNC_TX_KMAP_SRC	 = (1 << 5),
	ASYNC_TX_KMAP_DST	 = (1 << 6),
	ASYNC_TX_ASYNC_ONLY	 = (1 << 7),
};

#ifdef CONFIG_DMA_ENGINE
static inline enum dma_status
dma_wait_for_async_tx(struct dma_async_tx_descriptor *tx)
{
	enum dma_status status;
	struct dma_async_tx_descriptor *iter;

	if (!tx)
		return DMA_SUCCESS;

	/* poll through the dependency chain, return when tx is complete */
	do {
		iter = tx;
		while (iter->cookie == -EBUSY)
			iter = iter->parent;

		status = dma_sync_wait(iter->chan, iter->cookie);
	} while (status == DMA_IN_PROGRESS || (iter != tx));

	return status;
}

extern struct list_head async_tx_master_list;
static inline void async_tx_issue_pending_all(void)
{
	struct dma_chan_ref *ref;

	rcu_read_lock();
	list_for_each_entry_rcu(ref, &async_tx_master_list, node)
		ref->chan->device->device_issue_pending(ref->chan);
	rcu_read_unlock();
}

static inline void
async_tx_run_dependencies(struct dma_async_tx_descriptor *tx,
	struct dma_chan *host_chan)
{
	struct dma_async_tx_descriptor *dep_tx, *_dep_tx;
	struct dma_device *dev;
	struct dma_chan *chan;

	list_for_each_entry_safe(dep_tx, _dep_tx, &tx->depend_list,
		depend_node) {
		chan = dep_tx->chan;
		dev = chan->device;
		/* we can't depend on ourselves */
		BUG_ON(chan == host_chan);
		list_del(&dep_tx->depend_node);
		dev->device_tx_submit(dep_tx);

		/* we need to poke the engine as client code does not
		 * know about dependency submission events
		 */
		dev->device_issue_pending(chan);
	}
}
#else
static inline void
async_tx_run_dependencies(struct dma_async_tx_descriptor *tx,
	struct dma_chan *host_chan)
{
	do { } while (0);
}

static inline enum dma_status
dma_wait_for_async_tx(struct dma_async_tx_descriptor *tx)
{
	return DMA_SUCCESS;
}

static inline void async_tx_issue_pending_all(void)
{
	do { } while (0);
}
#endif

void
async_tx_submit(struct dma_chan *chan, struct dma_async_tx_descriptor *tx,
	enum async_tx_flags flags, struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback callback, void *callback_param);

struct dma_chan *
async_tx_find_channel(struct dma_async_tx_descriptor *depend_tx,
	enum dma_transaction_type tx_type);

void
async_tx_sync_epilog(unsigned long flags, struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback callback, void *callback_param);

struct dma_async_tx_descriptor *
async_xor(struct page *dest, struct page **src_list, unsigned int offset,
	int src_cnt, size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback callback, void *callback_param);

struct dma_async_tx_descriptor *
async_xor_zero_sum(struct page *dest, struct page **src_list,
	unsigned int offset, int src_cnt, size_t len,
	u32 *result, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback callback, void *callback_param);

struct dma_async_tx_descriptor *
async_memcpy(struct page *dest, struct page *src, unsigned int dest_offset,
	unsigned int src_offset, size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback callback, void *callback_param);

struct dma_async_tx_descriptor *
async_memset(struct page *dest, int val, unsigned int offset,
	size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback callback, void *callback_param);

struct dma_async_tx_descriptor *
async_trigger_callback(enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback callback, void *callback_param);

struct dma_async_tx_descriptor *
async_pqxor(struct page *pdest, struct page *qdest,
	struct page **src_list, unsigned char *scoef_list,
	unsigned int offset, int src_cnt, size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback callback, void *callback_param);

struct dma_async_tx_descriptor *
async_pqxor_zero_sum(struct page *pdest, struct page *qdest,
	struct page **src_list, unsigned char *scoef_list,
	unsigned int offset, int src_cnt, size_t len,
	u32 *presult, u32 *qresult, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback callback, void *callback_param);

struct dma_async_tx_descriptor *
async_r6_dd_recov (int src_num, size_t bytes, int faila, int failb, struct page **ptrs,
	enum async_tx_flags flags, struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback callback, void *callback_param);

struct dma_async_tx_descriptor *
async_r6_dp_recov (int src_num, size_t bytes, int faila, struct page **ptrs,
	enum async_tx_flags flags, struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback callback, void *callback_param);

#endif
