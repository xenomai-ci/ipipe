/* -*- linux-c -*-
 * linux/kernel/ipipe/compat.c
 *
 * Copyright (C) 2012 Philippe Gerum.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 * USA; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * I-pipe legacy interface.
 */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/ipipe.h>

static int ptd_key_count;

static unsigned long ptd_key_map;

IPIPE_DECLARE_SPINLOCK(__ipipe_lock);

void ipipe_init_attr(struct ipipe_domain_attr *attr)
{
	attr->name = "anon";
	attr->domid = 1;
	attr->entry = NULL;
	attr->priority = IPIPE_ROOT_PRIO;
	attr->pdd = NULL;
}
EXPORT_SYMBOL_GPL(ipipe_init_attr);

int ipipe_register_domain(struct ipipe_domain *ipd,
			  struct ipipe_domain_attr *attr)
{
	struct ipipe_percpu_domain_data *p;
	unsigned long flags;

	BUG_ON(attr->priority != IPIPE_HEAD_PRIORITY);

	ipipe_register_head(ipd, attr->name);
	ipd->domid = attr->domid;
	ipd->pdd = attr->pdd;
	ipd->priority = INT_MAX;

	if (attr->entry == NULL)
		return 0;

	local_irq_save_hw_smp(flags);
	__ipipe_current_domain = ipd;
	local_irq_restore_hw_smp(flags);
	attr->entry();
	local_irq_save_hw(flags);
	__ipipe_current_domain = ipipe_root_domain;
	p = ipipe_root_cpudom_ptr();

	if (__ipipe_ipending_p(p) &&
	    !test_bit(IPIPE_STALL_FLAG, &p->status))
		__ipipe_sync_stage();

	local_irq_restore_hw(flags);

	return 0;
}
EXPORT_SYMBOL_GPL(ipipe_register_domain);

int ipipe_unregister_domain(struct ipipe_domain *ipd)
{
	ipipe_unregister_head(ipd);

	return 0;
}
EXPORT_SYMBOL_GPL(ipipe_unregister_domain);

int ipipe_alloc_ptdkey(void)
{
	unsigned long flags;
	int key = -1;

	spin_lock_irqsave(&__ipipe_lock,flags);

	if (ptd_key_count < IPIPE_ROOT_NPTDKEYS) {
		key = ffz(ptd_key_map);
		set_bit(key,&ptd_key_map);
		ptd_key_count++;
	}

	spin_unlock_irqrestore(&__ipipe_lock,flags);

	return key;
}
EXPORT_SYMBOL_GPL(ipipe_alloc_ptdkey);

int ipipe_free_ptdkey(int key)
{
	unsigned long flags;

	if (key < 0 || key >= IPIPE_ROOT_NPTDKEYS)
		return -EINVAL;

	spin_lock_irqsave(&__ipipe_lock,flags);

	if (test_and_clear_bit(key,&ptd_key_map))
		ptd_key_count--;

	spin_unlock_irqrestore(&__ipipe_lock,flags);

	return 0;
}
EXPORT_SYMBOL_GPL(ipipe_free_ptdkey);

int ipipe_set_ptd(int key, void *value)
{
	if (key < 0 || key >= IPIPE_ROOT_NPTDKEYS)
		return -EINVAL;

	current->ptd[key] = value;

	return 0;
}
EXPORT_SYMBOL_GPL(ipipe_set_ptd);

void *ipipe_get_ptd(int key)
{
	if (key < 0 || key >= IPIPE_ROOT_NPTDKEYS)
		return NULL;

	return current->ptd[key];
}
EXPORT_SYMBOL_GPL(ipipe_get_ptd);

int ipipe_virtualize_irq(struct ipipe_domain *ipd,
			 unsigned int irq,
			 ipipe_irq_handler_t handler,
			 void *cookie,
			 ipipe_irq_ackfn_t ackfn,
			 unsigned int modemask)
{
	if (handler == NULL) {
		ipipe_free_irq(ipd, irq);
		return 0;
	}

	return ipipe_request_irq(ipd, irq, handler, cookie, ackfn,
				 modemask & IPIPE_IRQF_STICKY);
}
EXPORT_SYMBOL_GPL(ipipe_virtualize_irq);
