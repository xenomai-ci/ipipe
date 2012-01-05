/* -*- linux-c -*-
 * include/linux/ipipe_debug.h
 *
 * Copyright (C) 2012 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef __LINUX_IPIPE_DEBUG_H
#define __LINUX_IPIPE_DEBUG_H

#ifdef CONFIG_IPIPE

#include <linux/ipipe_percpu.h>

#ifdef CONFIG_IPIPE_DEBUG_CONTEXT

#include <asm/bug.h>

static inline int ipipe_disable_context_check(int cpu)
{
	return xchg(&per_cpu(ipipe_percpu_context_check, cpu), 0);
}

static inline void ipipe_restore_context_check(int cpu, int old_state)
{
	per_cpu(ipipe_percpu_context_check, cpu) = old_state;
}

static inline void ipipe_context_check_off(void)
{
	int cpu;
	for_each_online_cpu(cpu)
		per_cpu(ipipe_percpu_context_check, cpu) = 0;
}

static inline void ipipe_save_context_nmi(int cpu)
{
	per_cpu(ipipe_saved_context_check_state, cpu) =
		ipipe_disable_context_check(cpu);
}

static inline void ipipe_restore_context_nmi(int cpu)
{
	ipipe_restore_context_check
		(cpu, per_cpu(ipipe_saved_context_check_state, cpu));
}

#else	/* !CONFIG_IPIPE_DEBUG_CONTEXT */

static inline int ipipe_disable_context_check(int cpu)
{
	return 0;
}

static inline void ipipe_restore_context_check(int cpu, int old_state) { }

static inline void ipipe_context_check_off(void) { }

static inline void ipipe_save_context_nmi(int cpu) { }

static inline void ipipe_restore_context_nmi(int cpu) { }

#endif	/* !CONFIG_IPIPE_DEBUG_CONTEXT */

#ifdef CONFIG_IPIPE_DEBUG_INTERNAL
#define IPIPE_WARN(c)		WARN_ON(c)
#define IPIPE_WARN_ONCE(c)	WARN_ON_ONCE(c)
#else
#define IPIPE_WARN(c)		do { (void)(c); } while (0)
#define IPIPE_WARN_ONCE(c)	do { (void)(c); } while (0)
#endif

#ifdef CONFIG_IPIPE_DEBUG

static inline void ipipe_check_irqoff(void)
{
	if (WARN_ON_ONCE(!hard_irqs_disabled()))
		hard_local_irq_disable();
}

#else /* !CONFIG_IPIPE_DEBUG */

static inline void ipipe_check_irqoff(void) { }

#endif /* !CONFIG_IPIPE_DEBUG */

#endif /* CONFIG_IPIPE */

#endif /* !__LINUX_IPIPE_DEBUG_H */
