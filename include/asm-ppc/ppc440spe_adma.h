/*
 * 2006-2007 (C) DENX Software Engineering.
 *
 * Author: Yuri Tikhonov <yur@emcraft.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of
 * any kind, whether express or implied.
 */

#ifndef PPC440SPE_ADMA_H
#define PPC440SPE_ADMA_H

#include <linux/types.h>
#include <asm/ppc440spe_dma.h>
#include <asm/ppc440spe_xor.h>

#define PPC440SPE_ADMA_THRESHOLD	5

#define PPC440SPE_DMA0_ID	0
#define PPC440SPE_DMA1_ID	1
#define PPC440SPE_XOR_ID	2

#define PPC440SPE_DESC_INT	(1<<1)

#define PPC440SPE_ADMA_XOR_MAX_BYTE_COUNT (1 << 31) /* this is the XOR_CBBCR width */
#define PPC440SPE_ADMA_ZERO_SUM_MAX_BYTE_COUNT PPC440SPE_ADMA_XOR_MAX_BYTE_COUNT

#undef ADMA_LL_DEBUG

/**
 * struct ppc440spe_adma_device - internal representation of an ADMA device
 * @pdev: Platform device
 * @id: HW ADMA Device selector
 * @dma_desc_pool: base of DMA descriptor region (DMA address)
 * @dma_desc_pool_virt: base of DMA descriptor region (CPU address)
 * @common: embedded struct dma_device
 */
typedef struct ppc440spe_adma_device {
	struct platform_device *pdev;
	void *dma_desc_pool_virt;

	int id;
	dma_addr_t dma_desc_pool;
	struct dma_device common;
} ppc440spe_dev_t;

/**
 * struct ppc440spe_adma_chan - internal representation of an ADMA channel
 * @lock: serializes enqueue/dequeue operations to the slot pool
 * @device: parent device
 * @chain: device chain view of the descriptors
 * @common: common dmaengine channel object members
 * @all_slots: complete domain of slots usable by the channel
 * @pending: allows batching of hardware operations
 * @completed_cookie: identifier for the most recently completed operation
 * @slots_allocated: records the actual size of the descriptor slot pool
 * @irq_tasklet: bottom half where ppc440spe_adma_slot_cleanup runs
 */
typedef struct ppc440spe_adma_chan {
	spinlock_t lock;
	struct ppc440spe_adma_device *device;
	struct timer_list cleanup_watchdog;
	struct list_head chain;
	struct dma_chan common;
	struct list_head all_slots;
	struct ppc440spe_adma_desc_slot *last_used;
	int pending;
	dma_cookie_t completed_cookie;
	int slots_allocated;
	struct tasklet_struct irq_tasklet;
} ppc440spe_ch_t;

/**
 * struct ppc440spe_adma_desc_slot - PPC440SPE-ADMA software descriptor
 * @phys: hardware address of the hardware descriptor chain
 * @group_head: first operation in a transaction
 * @hw_next: pointer to the next descriptor in chain
 * @async_tx: support for the async_tx api
 * @slot_node: node on the iop_adma_chan.all_slots list
 * @chain_node: node on the op_adma_chan.chain list
 * @group_list: list of slots that make up a multi-descriptor transaction
 *      for example transfer lengths larger than the supported hw max
 * @unmap_len: transaction bytecount
 * @unmap_src_cnt: number of xor sources
 * @hw_desc: virtual address of the hardware descriptor chain
 * @stride: currently chained or not
 * @idx: pool index
 * @slot_cnt: total slots used in an transaction (group of operations)
 * @slots_per_op: number of slots per operation
 * @xor_check_result: result of zero sum
 * @flags: desc state
 * @crc32_result: result crc calculation
 */
typedef struct ppc440spe_adma_desc_slot {
	dma_addr_t phys;
	struct ppc440spe_adma_desc_slot *group_head;
	struct ppc440spe_adma_desc_slot *hw_next;
	struct dma_async_tx_descriptor async_tx;
	struct list_head slot_node;
	struct list_head chain_node; /* node in channel ops list */
	struct list_head group_list; /* list */
	unsigned int unmap_len;
	unsigned int unmap_src_cnt;
	void *hw_desc;
	u16 stride;
	u16 idx;
	u16 slot_cnt;
	u8 src_cnt;
	u8 slots_per_op;
	unsigned long flags;
	union {
		u32 *xor_check_result;
		u32 *crc32_result;
	};
} ppc440spe_desc_t;

typedef struct ppc440spe_adma_platform_data {
	int hw_id;
	unsigned long capabilities;
	size_t pool_size;
} ppc440spe_aplat_t;

#endif /* PPC440SPE_ADMA_H */
