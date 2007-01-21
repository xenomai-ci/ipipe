/*   -*- linux-c -*-
 *   include/asm-x86_64/ipipe.h
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

#ifndef __X86_64_IPIPE_H
#define __X86_64_IPIPE_H

#ifdef CONFIG_IPIPE

#include <linux/threads.h>

#define IPIPE_ARCH_STRING	"1.0-00"
#define IPIPE_MAJOR_NUMBER	1
#define IPIPE_MINOR_NUMBER	0
#define IPIPE_PATCH_NUMBER	0

/* Local APIC is always compiled in on x86_64.  Reserve 32 IRQs for
   APIC interrupts, we don't want them to mess with the normally
   assigned interrupts. */
#define IPIPE_NR_XIRQS	        (NR_IRQS + 32)
#define IPIPE_FIRST_APIC_IRQ   NR_IRQS

/* If the APIC is enabled, then we expose four service vectors in the
   APIC space which are freely available to domains. */
#define IPIPE_SERVICE_VECTOR0	(INVALIDATE_TLB_VECTOR_END + 1)
#define IPIPE_SERVICE_IPI0	(IPIPE_SERVICE_VECTOR0 - FIRST_EXTERNAL_VECTOR)
#define IPIPE_SERVICE_VECTOR1	(INVALIDATE_TLB_VECTOR_END + 2)
#define IPIPE_SERVICE_IPI1	(IPIPE_SERVICE_VECTOR1 - FIRST_EXTERNAL_VECTOR)
#define IPIPE_SERVICE_VECTOR2	(INVALIDATE_TLB_VECTOR_END + 3)
#define IPIPE_SERVICE_IPI2	(IPIPE_SERVICE_VECTOR2 - FIRST_EXTERNAL_VECTOR)
#define IPIPE_SERVICE_VECTOR3	(INVALIDATE_TLB_VECTOR_END + 4)
#define IPIPE_SERVICE_IPI3	(IPIPE_SERVICE_VECTOR3 - FIRST_EXTERNAL_VECTOR)

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

#ifndef __ASSEMBLY__

#include <linux/cpumask.h>
#include <linux/list.h>
#include <linux/threads.h>
#include <asm/ptrace.h>

#ifdef CONFIG_SMP

#include <asm/mpspec.h>
#include <linux/thread_info.h>

#define IPIPE_CRITICAL_VECTOR  0xf8	/* Used by ipipe_critical_enter/exit() */
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

#define ipipe_read_tsc(t)  do {		\
	unsigned int __a,__d;			\
	asm volatile("rdtsc" : "=a" (__a), "=d" (__d)); \
	(t) = ((unsigned long)__a) | (((unsigned long)__d)<<32); \
} while(0)

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

int __ipipe_handle_irq(struct pt_regs *regs);

void __ipipe_do_critical_sync(unsigned irq, void *cookie);

void __ipipe_serial_debug(const char *fmt, ...);

extern struct pt_regs __ipipe_tick_regs[];

extern int __ipipe_tick_irq;

unsigned __ipipe_get_irq_vector(int irq);

asmlinkage void __ipipe_root_xirq_thunk(unsigned irq,
					void (*handler)(unsigned irq, void *cookie));

asmlinkage void __ipipe_root_virq_thunk(void (*handler)(unsigned irq, void *cookie),
					unsigned irq,
					void *cookie);

static inline unsigned long __ipipe_ffnz(unsigned long ul)
{
      __asm__("bsrq %1, %0":"=r"(ul)
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
			__ipipe_root_xirq_thunk(~__ipipe_get_irq_vector(irq), (ipd)->irqs[irq].handler); \
		} else {					 \
			irq_enter();				 \
			__ipipe_root_virq_thunk(	\
				(ipd)->irqs[irq].handler, irq, (ipd)->irqs[irq].cookie); \
			irq_exit();				 \
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
	(((p)->flags & PF_EVNOTIFY) || (unsigned long)sc >= __NR_syscall_max + 1)

#else /* !CONFIG_IPIPE */

#define task_hijacked(p)	0

#define NR_XIRQS NR_IRQS

#define ipipe_note_apicid(apicid,cpu)  do { } while(0)

#endif /* CONFIG_IPIPE */

#endif	/* !__I386_IPIPE_H */
