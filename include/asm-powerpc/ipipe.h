/* -*- linux-c -*-
 * include/asm-ppc/ipipe.h
 *
 * Copyright (C) 2002-2007 Philippe Gerum.
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

#ifndef __POWERPC_IPIPE_H
#define __POWERPC_IPIPE_H

#ifdef CONFIG_IPIPE

#include <asm/ptrace.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/time.h>
#include <linux/list.h>
#include <linux/cpumask.h>
#include <linux/threads.h>

#define IPIPE_ARCH_STRING	"1.6-04"
#define IPIPE_MAJOR_NUMBER	1
#define IPIPE_MINOR_NUMBER	6
#define IPIPE_PATCH_NUMBER	4

#define IPIPE_NR_XIRQS		NR_IRQS
#define IPIPE_IRQ_ISHIFT	5	/* 2^5 for 32bits arch. */

/*
 * The first virtual interrupt is reserved for the timer (see
 * __ipipe_init_platform).
 */
#define IPIPE_TIMER_VIRQ	IPIPE_VIRQ_BASE

#ifdef CONFIG_SMP
#error "I-pipe/powerpc: SMP not yet implemented"
#define ipipe_processor_id()	(current_thread_info()->cpu)
#else /* !CONFIG_SMP */
#define ipipe_processor_id()	0
#endif	/* CONFIG_SMP */

#define prepare_arch_switch(next)				\
do {								\
	ipipe_schedule_notify(current, next);			\
	local_irq_disable_hw();					\
} while(0)

#define task_hijacked(p)					\
	({							\
	int x = ipipe_current_domain != ipipe_root_domain;	\
	__clear_bit(IPIPE_SYNC_FLAG,				\
		    &ipipe_root_domain->cpudata[task_cpu(p)].status); \
	local_irq_enable_hw(); x;				\
	})

 /* PPC traps */
#define IPIPE_TRAP_ACCESS	 0	/* Data or instruction access exception */
#define IPIPE_TRAP_ALIGNMENT	 1	/* Alignment exception */
#define IPIPE_TRAP_ALTUNAVAIL	 2	/* Altivec unavailable */
#define IPIPE_TRAP_PCE		 3	/* Program check exception */
#define IPIPE_TRAP_MCE		 4	/* Machine check exception */
#define IPIPE_TRAP_UNKNOWN	 5	/* Unknown exception */
#define IPIPE_TRAP_IABR		 6	/* Instruction breakpoint */
#define IPIPE_TRAP_RM		 7	/* Run mode exception */
#define IPIPE_TRAP_SSTEP	 8	/* Single-step exception */
#define IPIPE_TRAP_NREC		 9	/* Non-recoverable exception */
#define IPIPE_TRAP_SOFTEMU	10	/* Software emulation */
#define IPIPE_TRAP_DEBUG	11	/* Debug exception */
#define IPIPE_TRAP_SPE		12	/* SPE exception */
#define IPIPE_TRAP_ALTASSIST	13	/* Altivec assist exception */
#define IPIPE_TRAP_CACHE	14	/* Cache-locking exception (FSL) */
#define IPIPE_TRAP_FPUNAVAIL	15	/* FP unavailable exception */
#define IPIPE_NR_FAULTS		16
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

#define ipipe_read_tsc(t)					\
	({							\
	unsigned long __tbu;					\
	__asm__ __volatile__ ("1: mftbu %0\n"			\
			      "mftb %1\n"			\
			      "mftbu %2\n"			\
			      "cmpw %2,%0\n"			\
			      "bne- 1b\n"			\
			      :"=r" (((unsigned long *)&t)[0]),	\
			      "=r" (((unsigned long *)&t)[1]),	\
			      "=r" (__tbu));			\
	t;							\
	})

#define __ipipe_read_timebase()					\
	({							\
	unsigned long long t;					\
	ipipe_read_tsc(t);					\
	t;							\
	})

#define ipipe_cpu_freq()	(HZ * tb_ticks_per_jiffy)
#define ipipe_tsc2ns(t)		((((unsigned long)(t)) * 1000) / (ipipe_cpu_freq() / 1000000))

#define ipipe_tsc2us(t) \
({ \
    unsigned long long delta = (t); \
    do_div(delta, ipipe_cpu_freq()/1000000+1);	\
    (unsigned long)delta; \
})

/* Private interface -- Internal use only */

#define __ipipe_check_platform()	do { } while(0)

#define __ipipe_enable_irq(irq)	enable_irq(irq)

#define __ipipe_disable_irq(irq)	disable_irq(irq)

#ifdef CONFIG_SMP
void __ipipe_hook_critical_ipi(struct ipipe_domain *ipd);
#else
#define __ipipe_hook_critical_ipi(ipd) do { } while(0)
#endif

void __ipipe_enable_irqdesc(unsigned irq);

void __ipipe_init_platform(void);

void __ipipe_enable_pipeline(void);

extern unsigned long __ipipe_decr_ticks;

extern unsigned long long __ipipe_decr_next[];

extern struct pt_regs __ipipe_tick_regs[];

extern unsigned long disarm_decr[];

void __ipipe_handle_irq(int irq,
			struct pt_regs *regs);

void __ipipe_serial_debug(const char *fmt, ...);

#define __ipipe_tick_irq	IPIPE_TIMER_VIRQ

static inline unsigned long __ipipe_ffnz(unsigned long ul)
{
	__asm__ __volatile__("cntlzw %0, %1":"=r"(ul):"r"(ul & (-ul)));
	return 31 - ul;
}

/* When running handlers, enable hw interrupts for all domains but the
 * one heading the pipeline, so that IRQs can never be significantly
 * deferred for the latter. */
#define __ipipe_run_isr(ipd, irq, cpuid)				\
do {									\
	local_irq_enable_nohead(ipd);					\
	if (ipd == ipipe_root_domain)					\
		if (likely(!ipipe_virtual_irq_p(irq)))			\
			ipd->irqs[irq].handler(irq, NULL);		\
		else {							\
			irq_enter();					\
			ipd->irqs[irq].handler(irq, ipd->irqs[irq].cookie);\
			irq_exit();					\
		}							\
	else {								\
		__clear_bit(IPIPE_SYNC_FLAG, &cpudata->status);		\
		ipd->irqs[irq].handler(irq, ipd->irqs[irq].cookie);	\
		__set_bit(IPIPE_SYNC_FLAG, &cpudata->status);		\
	}								\
	local_irq_disable_nohead(ipd);					\
} while(0)

#define __ipipe_syscall_watched_p(p, sc)	\
	(((p)->flags & PF_EVNOTIFY) || (unsigned long)sc >= NR_syscalls)

#else /* !CONFIG_IPIPE */

#define task_hijacked(p)	0

#endif /* CONFIG_IPIPE */

#endif	/* !__POWERPC_IPIPE_H */

