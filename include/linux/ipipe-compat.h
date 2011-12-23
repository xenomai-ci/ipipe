/* -*- linux-c -*-
 * include/linux/ipipe-compat.h
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
 */

#ifndef __LINUX_IPIPE_COMPAT_H
#define __LINUX_IPIPE_COMPAT_H

#ifndef __LINUX_IPIPE_H
#error "Do not include this file directly, use linux/ipipe.h instead"
#endif

#ifdef CONFIG_IPIPE

#define IPIPE_HEAD_PRIORITY	(-1)
#define IPIPE_ROOT_PRIO		100
#define IPIPE_ROOT_ID		0

struct ipipe_domain_attr {
	unsigned int domid;
	const char *name;
	int priority;
	void (*entry) (void);
	void *pdd;
};

void ipipe_init_attr(struct ipipe_domain_attr *attr);

int ipipe_register_domain(struct ipipe_domain *ipd,
			  struct ipipe_domain_attr *attr);

int ipipe_unregister_domain(struct ipipe_domain *ipd);

static inline void ipipe_check_context(struct ipipe_domain *border_ipd)
{
#ifdef CONFIG_IPIPE_DEBUG_CONTEXT
	ipipe_root_only();
#endif /* !CONFIG_IPIPE_DEBUG_CONTEXT */
}

#else /* !CONFIG_IPIPE */

#define ipipe_check_context(ipd)	do { } while(0)

#endif /* !CONFIG_IPIPE */

#endif	/* !__LINUX_IPIPE_COMPAT_H */
