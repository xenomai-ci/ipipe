/* -*- linux-c -*-
 * arch/x86/include/asm/ipipe_base_32.h
 *
 * Copyright (C) 2007 Philippe Gerum.
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

#ifndef __X86_IPIPE_BASE_32_H
#define __X86_IPIPE_BASE_32_H

#include <linux/threads.h>
#include <asm/apicdef.h>
#include <asm/irq_vectors.h>

#ifdef CONFIG_X86_LOCAL_APIC
/* System interrupts are mapped beyond the last defined external IRQ
 * number. */
#define IPIPE_FIRST_APIC_IRQ	NR_IRQS
#define IPIPE_NR_XIRQS		(NR_IRQS + 256 - FIRST_SYSTEM_VECTOR)
#define ipipe_apic_irq_vector(irq)  ((irq) - IPIPE_FIRST_APIC_IRQ + FIRST_SYSTEM_VECTOR)
#define ipipe_apic_vector_irq(vec)  ((vec) - FIRST_SYSTEM_VECTOR + IPIPE_FIRST_APIC_IRQ)
/* If the APIC is enabled, then we expose four service vectors in the
 * APIC space which are freely available to domains. */
#define IPIPE_SERVICE_VECTOR0	0xf5
#define IPIPE_SERVICE_IPI0	ipipe_apic_vector_irq(IPIPE_SERVICE_VECTOR0)
#define IPIPE_SERVICE_VECTOR1	0xf6
#define IPIPE_SERVICE_IPI1	ipipe_apic_vector_irq(IPIPE_SERVICE_VECTOR1)
#define IPIPE_SERVICE_VECTOR2	0xf7
#define IPIPE_SERVICE_IPI2	ipipe_apic_vector_irq(IPIPE_SERVICE_VECTOR2)
#define IPIPE_SERVICE_VECTOR3	0xf8
#define IPIPE_SERVICE_IPI3	ipipe_apic_vector_irq(IPIPE_SERVICE_VECTOR3)
#define IPIPE_CRITICAL_VECTOR	0xf9	/* SMP-only: used by ipipe_critical_enter/exit() */
#define IPIPE_CRITICAL_IPI	ipipe_apic_vector_irq(IPIPE_CRITICAL_VECTOR)
#else	/* !CONFIG_X86_LOCAL_APIC */
#define IPIPE_NR_XIRQS		NR_IRQS
#endif	/* !CONFIG_X86_LOCAL_APIC */

#define IPIPE_IRQ_ISHIFT  	5	/* 2^5 for 32bits arch. */

/* IDT fault vectors */
#define IPIPE_NR_FAULTS		33 /* 32 from IDT + iret_error */
/* Pseudo-vectors used for kernel events */
#define IPIPE_FIRST_EVENT	IPIPE_NR_FAULTS
#define IPIPE_EVENT_SYSCALL	(IPIPE_FIRST_EVENT)
#define IPIPE_EVENT_SCHEDULE	(IPIPE_FIRST_EVENT + 1)
#define IPIPE_EVENT_SIGWAKE	(IPIPE_FIRST_EVENT + 2)
#define IPIPE_EVENT_SETSCHED	(IPIPE_FIRST_EVENT + 3)
#define IPIPE_EVENT_INIT	(IPIPE_FIRST_EVENT + 4)
#define IPIPE_EVENT_EXIT	(IPIPE_FIRST_EVENT + 5)
#define IPIPE_EVENT_CLEANUP	(IPIPE_FIRST_EVENT + 6)
#define IPIPE_LAST_EVENT	IPIPE_EVENT_CLEANUP
#define IPIPE_NR_EVENTS		(IPIPE_LAST_EVENT + 1)

#ifndef __ASSEMBLY__

#include <asm/alternative.h>

#ifdef CONFIG_SMP

#define GET_ROOT_STATUS_ADDR					\
	"pushfl; cli;"						\
	"movl %%fs:per_cpu__this_cpu_off, %%eax;"		\
	"lea per_cpu__ipipe_percpu_darray(%%eax), %%eax;"
#define PUT_ROOT_STATUS_ADDR	"popfl;"

static inline void __ipipe_stall_root(void)
{
	__asm__ __volatile__(GET_ROOT_STATUS_ADDR
			     LOCK_PREFIX
			     "btsl $0,(%%eax);"
			     PUT_ROOT_STATUS_ADDR
			     : : : "eax", "memory");
}

static inline unsigned long __ipipe_test_and_stall_root(void)
{
	int oldbit;

	__asm__ __volatile__(GET_ROOT_STATUS_ADDR
			     LOCK_PREFIX
			     "btsl $0,(%%eax);"
			     "sbbl %0,%0;"
			     PUT_ROOT_STATUS_ADDR
			     :"=r" (oldbit)
			     : : "eax", "memory");
	return oldbit;
}

static inline unsigned long __ipipe_test_root(void)
{
	int oldbit;

	__asm__ __volatile__(GET_ROOT_STATUS_ADDR
			     "btl $0,(%%eax);"
			     "sbbl %0,%0;"
			     PUT_ROOT_STATUS_ADDR
			     :"=r" (oldbit)
			     : : "eax");
	return oldbit;
}

#endif	/* CONFIG_SMP */

void __ipipe_serial_debug(const char *fmt, ...);

#endif /* !__ASSEMBLY__ */

#endif	/* !__X86_IPIPE_BASE_32_H */
