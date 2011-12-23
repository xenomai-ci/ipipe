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
#include <linux/ipipe.h>

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
