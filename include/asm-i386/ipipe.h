/*   -*- linux-c -*-
 *   include/asm-i386/ipipe.h
 *
 *   Copyright (C) 2002-2005 Philippe Gerum.
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

#ifndef __I386_IPIPE_H
#define __I386_IPIPE_H

#ifdef CONFIG_IPIPE

#include <linux/threads.h>
#include <irq_vectors.h>

#define IPIPE_ARCH_STRING	"1.6-04"
#define IPIPE_MAJOR_NUMBER	1
#define IPIPE_MINOR_NUMBER	6
#define IPIPE_PATCH_NUMBER	4

#ifdef CONFIG_X86_LOCAL_APIC
/* We want to cover the whole IRQ space when the APIC is enabled. */
#ifdef CONFIG_PCI_MSI
#define IPIPE_NR_XIRQS NR_IRQS
#else	/* CONFIG_PCI_MSI */
#define IPIPE_NR_XIRQS   224
#endif	/* CONFIG_PCI_MSI */
/* If the APIC is enabled, then we expose four service vectors in the
   APIC space which are freely available to domains. */
#define IPIPE_SERVICE_VECTOR0	0xf5
#define IPIPE_SERVICE_IPI0	(IPIPE_SERVICE_VECTOR0 - FIRST_EXTERNAL_VECTOR)
#define IPIPE_SERVICE_VECTOR1	0xf6
#define IPIPE_SERVICE_IPI1	(IPIPE_SERVICE_VECTOR1 - FIRST_EXTERNAL_VECTOR)
#define IPIPE_SERVICE_VECTOR2	0xf7
#define IPIPE_SERVICE_IPI2	(IPIPE_SERVICE_VECTOR2 - FIRST_EXTERNAL_VECTOR)
#define IPIPE_SERVICE_VECTOR3	0xf8
#define IPIPE_SERVICE_IPI3	(IPIPE_SERVICE_VECTOR3 - FIRST_EXTERNAL_VECTOR)
#else	/* !CONFIG_X86_LOCAL_APIC */
#define IPIPE_NR_XIRQS		NR_IRQS
#endif	/* CONFIG_X86_LOCAL_APIC */

#define IPIPE_IRQ_ISHIFT  	5	/* 2^5 for 32bits arch. */
#define NR_XIRQS		IPIPE_NR_XIRQS

#define ex_do_divide_error		0
#define ex_do_debug			1
/* NMI not pipelined. */
#define ex_do_int3			3
#define ex_do_overflow			4
#define ex_do_bounds			5
#define ex_do_invalid_op		6
#define ex_device_not_available		7
/* Double fault not pipelined. */
#define ex_do_coprocessor_segment_overrun 9
#define ex_do_invalid_TSS		10
#define ex_do_segment_not_present	11
#define ex_do_stack_segment		12
#define ex_do_general_protection	13
#define ex_do_page_fault		14
#define ex_do_spurious_interrupt_bug	15
#define ex_do_coprocessor_error		16
#define ex_do_alignment_check		17
#define ex_machine_check_vector		18
#define ex_do_simd_coprocessor_error	19
#define ex_do_iret_error		32

#ifndef __ASSEMBLY__

#include <linux/cpumask.h>
#include <linux/list.h>
#include <linux/threads.h>
#include <asm/ptrace.h>

#ifdef CONFIG_SMP

#include <asm/fixmap.h>
#include <asm/mpspec.h>
#include <mach_apicdef.h>
#include <linux/thread_info.h>

#define IPIPE_CRITICAL_VECTOR  0xf9	/* Used by ipipe_critical_enter/exit() */
#define IPIPE_CRITICAL_IPI     (IPIPE_CRITICAL_VECTOR - FIRST_EXTERNAL_VECTOR)

extern int (*__ipipe_logical_cpuid)(void);

#define ipipe_processor_id()  __ipipe_logical_cpuid()

extern u8 __ipipe_apicid_2_cpu[];

#define ipipe_note_apicid(apicid,cpu)  \
do {	\
	__ipipe_apicid_2_cpu[apicid] = cpu; \
} while(0)

#else	/* !CONFIG_SMP */

#define ipipe_note_apicid(apicid,cpu)  do { } while(0)
#define ipipe_processor_id()    0

#endif	/* !CONFIG_SMP */

#define prepare_arch_switch(next)		\
do {						\
	ipipe_schedule_notify(current, next);	\
	local_irq_disable_hw();			\
} while(0)

#define task_hijacked(p)	\
  ({ int x = ipipe_current_domain != ipipe_root_domain; \
     __clear_bit(IPIPE_SYNC_FLAG,&ipipe_root_domain->cpudata[task_cpu(p)].status); \
     local_irq_enable_hw(); x; })

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

struct ipipe_domain;

struct ipipe_sysinfo {

	int ncpus;		/* Number of CPUs on board */
	u64 cpufreq;		/* CPU frequency (in Hz) */

	/* Arch-dependent block */

	struct {
		unsigned tmirq;	/* Timer tick IRQ */
		u64 tmfreq;	/* Timer frequency */
	} archdep;
};

#define ipipe_read_tsc(t)  __asm__ __volatile__("rdtsc" : "=A" (t))
#define ipipe_cpu_freq() ({ unsigned long long __freq = cpu_has_tsc?(1000LL * cpu_khz):CLOCK_TICK_RATE; __freq; })

#define ipipe_tsc2ns(t) \
({ \
	unsigned long long delta = (t)*1000; \
	do_div(delta, cpu_khz/1000+1); \
	(unsigned long)delta; \
})

#define ipipe_tsc2us(t) \
({ \
    unsigned long long delta = (t); \
    do_div(delta, cpu_khz/1000+1); \
    (unsigned long)delta; \
})

/* Private interface -- Internal use only */

#define __ipipe_check_platform() do { } while(0)

#define __ipipe_init_platform() do { } while(0)

#define __ipipe_enable_irq(irq)	irq_desc[irq].chip->enable(irq)

#define __ipipe_disable_irq(irq)	irq_desc[irq].chip->disable(irq)

#ifdef CONFIG_SMP
void __ipipe_hook_critical_ipi(struct ipipe_domain *ipd);
#else
#define __ipipe_hook_critical_ipi(ipd) do { } while(0)
#endif

void __ipipe_enable_irqdesc(unsigned irq);

void __ipipe_enable_pipeline(void);

int __ipipe_handle_irq(struct pt_regs regs);

void __ipipe_do_critical_sync(unsigned irq, void *cookie);

extern struct pt_regs __ipipe_tick_regs[];

extern int __ipipe_tick_irq;

#define __ipipe_call_root_xirq_handler(ipd,irq) \
   __asm__ __volatile__ ("pushfl\n\t" \
                         "pushl %%cs\n\t" \
                         "pushl $1f\n\t" \
	                 "pushl %%eax\n\t" \
	                 "pushl %%es\n\t" \
	                 "pushl %%ds\n\t" \
	                 "pushl %%eax\n\t" \
	                 "pushl %%ebp\n\t" \
	                 "pushl %%edi\n\t" \
	                 "pushl %%esi\n\t" \
	                 "pushl %%edx\n\t" \
	                 "pushl %%ecx\n\t" \
	                 "pushl %%ebx\n\t" \
                         "movl  %%esp,%%eax\n\t" \
                         "call *%1\n\t" \
	                 "jmp ret_from_intr\n\t" \
	                 "1:\n" \
			 : /* no output */ \
			 : "a" (~irq), "m" ((ipd)->irqs[irq].handler))

#define __ipipe_call_root_virq_handler(ipd,irq) \
   __asm__ __volatile__ ("pushfl\n\t" \
                         "pushl %%cs\n\t" \
                         "pushl $__virq_end\n\t" \
	                 "pushl $-1\n\t" \
	                 "pushl %%es\n\t" \
	                 "pushl %%ds\n\t" \
	                 "pushl %%eax\n\t" \
	                 "pushl %%ebp\n\t" \
	                 "pushl %%edi\n\t" \
	                 "pushl %%esi\n\t" \
	                 "pushl %%edx\n\t" \
	                 "pushl %%ecx\n\t" \
	                 "pushl %%ebx\n\t" \
			 "pushl %2\n\t" \
                         "pushl %%eax\n\t" \
                         "call *%1\n\t" \
			 "addl $8,%%esp\n\t" \
			 : /* no output */ \
			 : "a" (irq), "m" ((ipd)->irqs[irq].handler), "r" ((ipd)->irqs[irq].cookie))

#define __ipipe_finalize_root_virq_handler() \
   __asm__ __volatile__ ("jmp ret_from_intr\n\t" \
	                 "__virq_end:\n" \
			 : /* no output */ \
			 : /* no input */)

static inline unsigned long __ipipe_ffnz(unsigned long ul)
{
      __asm__("bsrl %1, %0":"=r"(ul)
      :	"r"(ul));
	return ul;
}

/* When running handlers, enable hw interrupts for all domains but the
 * one heading the pipeline, so that IRQs can never be significantly
 * deferred for the latter. */
#define __ipipe_run_isr(ipd, irq, cpuid) \
do { \
	local_irq_enable_nohead(ipd);				 \
	if (ipd == ipipe_root_domain) {				 \
		if (likely(!ipipe_virtual_irq_p(irq))) {	 \
			__ipipe_call_root_xirq_handler(ipd,irq); \
		} else {					 \
			irq_enter();				 \
			__ipipe_call_root_virq_handler(ipd,irq); \
			irq_exit();				 \
			__ipipe_finalize_root_virq_handler();	 \
		}						\
	} else {						\
		__clear_bit(IPIPE_SYNC_FLAG, &cpudata->status); \
		ipd->irqs[irq].handler(irq, ipd->irqs[irq].cookie); \
		__set_bit(IPIPE_SYNC_FLAG, &cpudata->status);	\
	}							\
	local_irq_disable_nohead(ipd);				\
} while(0)

#endif /* __ASSEMBLY__ */

#define __ipipe_syscall_watched_p(p, sc)	\
	(((p)->flags & PF_EVNOTIFY) || (unsigned long)sc >= NR_syscalls)

#else /* !CONFIG_IPIPE */

#define task_hijacked(p)	0

#define NR_XIRQS NR_IRQS

#define ipipe_note_apicid(apicid,cpu)  do { } while(0)

#endif /* CONFIG_IPIPE */

#endif	/* !__I386_IPIPE_H */
