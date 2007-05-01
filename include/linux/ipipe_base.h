/* -*- linux-c -*-
 * include/linux/ipipe_base.h
 *
 * Copyright (C) 2002-2007 Philippe Gerum.
 *               2007 Jan Kiszka.
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

#ifndef __LINUX_IPIPE_BASE_H
#define __LINUX_IPIPE_BASE_H

#ifdef CONFIG_IPIPE

/* Per-cpu pipeline status */
#define IPIPE_STALL_FLAG	0	/* Stalls a pipeline stage -- guaranteed at bit #0 */
#define IPIPE_SYNC_FLAG		1	/* The interrupt syncer is running for the domain */
#define IPIPE_NOSTACK_FLAG	2	/* Domain currently runs on a foreign stack */

#define IPIPE_SYNC_MASK		(1 << IPIPE_SYNC_FLAG)

extern struct ipipe_domain ipipe_root;

#define ipipe_root_domain (&ipipe_root)

#ifdef CONFIG_SMP

void __ipipe_stall_root(void);

unsigned long __ipipe_test_root(void);

unsigned long __ipipe_test_and_stall_root(void);

#else /* !CONFIG_SMP */

/*
 * Note: This cast relies on cpudata[0].status being the first element in the
 *       root domain structure (for UP only).
 */
#define __ipipe_root_status	(unsigned long *)&ipipe_root

static inline void __ipipe_stall_root(void)
{
	set_bit(IPIPE_STALL_FLAG, __ipipe_root_status);
}

static inline unsigned long __ipipe_test_root(void)
{
	return test_bit(IPIPE_STALL_FLAG, __ipipe_root_status);
}

static inline unsigned long __ipipe_test_and_stall_root(void)
{
	return test_and_set_bit(IPIPE_STALL_FLAG, __ipipe_root_status);
}

#endif /* !CONFIG_SMP */

void __ipipe_unstall_root(void);

void __ipipe_restore_root(unsigned long x);

#ifdef CONFIG_IPIPE_DEBUG_CONTEXT
void ipipe_check_context(struct ipipe_domain *border_ipd);
#else /* !CONFIG_IPIPE_DEBUG_CONTEXT */
static inline void ipipe_check_context(struct ipipe_domain *border_ipd) { }
#endif /* !CONFIG_IPIPE_DEBUG_CONTEXT */

#endif	/* CONFIG_IPIPE */

#endif	/* !__LINUX_IPIPE_BASE_H */
