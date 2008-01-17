/*   -*- linux-c -*-
 *   include/asm-x86/ipipe_base_64.h
 *
 *   Copyright (C) 2007 Philippe Gerum.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __X86_IPIPE_BASE_64_H
#define __X86_IPIPE_BASE_64_H

#include <linux/threads.h>

/* Local APIC is always compiled in on x86_64.  Reserve 32 IRQs for
   APIC interrupts, we don't want them to mess with the normally
   assigned interrupts. */
#define IPIPE_NR_XIRQS		(NR_IRQS + 32)
#define IPIPE_FIRST_APIC_IRQ	NR_IRQS

#define ipipe_apic_irq_vector(irq)  ((irq) - IPIPE_FIRST_APIC_IRQ + FIRST_SYSTEM_VECTOR)
#define ipipe_apic_vector_irq(vec)  ((vec) - FIRST_SYSTEM_VECTOR + IPIPE_FIRST_APIC_IRQ)

/* If the APIC is enabled, then we expose four service vectors in the
   APIC space which are freely available to domains. */
#define IPIPE_SERVICE_VECTOR0	(INVALIDATE_TLB_VECTOR_END + 1)
#define IPIPE_SERVICE_IPI0	ipipe_apic_vector_irq(IPIPE_SERVICE_VECTOR0)
#define IPIPE_SERVICE_VECTOR1	(INVALIDATE_TLB_VECTOR_END + 2)
#define IPIPE_SERVICE_IPI1	ipipe_apic_vector_irq(IPIPE_SERVICE_VECTOR1)
#define IPIPE_SERVICE_VECTOR2	(INVALIDATE_TLB_VECTOR_END + 3)
#define IPIPE_SERVICE_IPI2	ipipe_apic_vector_irq(IPIPE_SERVICE_VECTOR2)
#define IPIPE_SERVICE_VECTOR3	(INVALIDATE_TLB_VECTOR_END + 4)
#define IPIPE_SERVICE_IPI3	ipipe_apic_vector_irq(IPIPE_SERVICE_VECTOR3)
#ifdef CONFIG_SMP
#define IPIPE_CRITICAL_VECTOR	0xf8	/* Used by ipipe_critical_enter/exit() */
#define IPIPE_CRITICAL_IPI	ipipe_apic_vector_irq(IPIPE_CRITICAL_VECTOR)
#endif

#define IPIPE_IRQ_ISHIFT  	6	/* 2^6 for 64bits arch. */

#define ex_divide_error			0
#define ex_debug			1
/* NMI not pipelined. */
#define ex_int3				3
#define ex_overflow			4
#define ex_bounds			5
#define ex_invalid_op			6
#define ex_math_state_restore		7
/* Double fault not pipelined. */
#define ex_coprocessor_segment_overrun	9
#define ex_invalid_TSS			10
#define ex_segment_not_present		11
#define ex_stack_segment		12
#define ex_general_protection		13
#define ex_page_fault			14
#define ex_spurious_interrupt_bug	15
#define ex_coprocessor_error		16
#define ex_alignment_check		17
#define ex_reserved			18
#define ex_machine_check_vector		ex_reserved
#define ex_simd_coprocessor_error	19

/* IDT fault vectors */
#define IPIPE_NR_FAULTS		32
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
/*
 * Ugly: depends on x8664_pda layout and actual implementation of
 * percpu accesses.
 */
#define GET_ROOT_STATUS_ADDR					\
	"pushfq; cli;"						\
	"movq %%gs:8, %%rax;" /* x8664_pda.data_offset */	\
	"addq $per_cpu__ipipe_percpu_darray, %%rax;"
#define PUT_ROOT_STATUS_ADDR	"popfq;"

static inline void __ipipe_stall_root(void)
{
	__asm__ __volatile__(GET_ROOT_STATUS_ADDR
			     LOCK_PREFIX
			     "btsl $0,(%%rax);"
			     PUT_ROOT_STATUS_ADDR
			     : : : "rax", "memory");
}

static inline unsigned long __ipipe_test_and_stall_root(void)
{
	int oldbit;

	__asm__ __volatile__(GET_ROOT_STATUS_ADDR
			     LOCK_PREFIX
			     "btsl $0,(%%rax);"
			     "sbbl %0,%0;"
			     PUT_ROOT_STATUS_ADDR
			     :"=r" (oldbit)
			     : : "rax", "memory");
	return oldbit;
}

static inline unsigned long __ipipe_test_root(void)
{
	int oldbit;

	__asm__ __volatile__(GET_ROOT_STATUS_ADDR
			     "btl $0,(%%rax);"
			     "sbbl %0,%0;"
			     PUT_ROOT_STATUS_ADDR
			     :"=r" (oldbit)
			     : : "rax");
	return oldbit;
}

#else /* !CONFIG_SMP */

#if __GNUC__ >= 4
/* Alias to ipipe_root_cpudom_var(status) */
extern unsigned long __ipipe_root_status;
#else
extern unsigned long *const __ipipe_root_status_addr;
#define __ipipe_root_status	(*__ipipe_root_status_addr)
#endif

static inline void __ipipe_stall_root(void)
{
	volatile unsigned long *p = &__ipipe_root_status;
	__asm__ __volatile__("btsl $0,%0;"
			     :"+m" (*p) : : "memory");
}

static inline unsigned long __ipipe_test_and_stall_root(void)
{
	volatile unsigned long *p = &__ipipe_root_status;
	int oldbit;

	__asm__ __volatile__("btsl $0,%1;"
			     "sbbl %0,%0;"
			     :"=r" (oldbit), "+m" (*p)
			     : : "memory");
	return oldbit;
}

static inline unsigned long __ipipe_test_root(void)
{
	volatile unsigned long *p = &__ipipe_root_status;
	int oldbit;

	__asm__ __volatile__("btl $0,%1;"
			     "sbbl %0,%0;"
			     :"=r" (oldbit)
			     :"m" (*p));
	return oldbit;
}

#endif	/* CONFIG_SMP */

#endif /* !__ASSEMBLY__ */

#endif	/* !__X86_IPIPE_BASE_64_H */
