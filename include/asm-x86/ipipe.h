/*   -*- linux-c -*-
 *   include/asm-x86/ipipe.h
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

#define IPIPE_ARCH_STRING	"1.11-01"
#define IPIPE_MAJOR_NUMBER	1
#define IPIPE_MINOR_NUMBER	11
#define IPIPE_PATCH_NUMBER	1

#ifndef __ASSEMBLY__

#include <linux/cpumask.h>
#include <linux/list.h>
#include <linux/threads.h>
#include <linux/ipipe_percpu.h>
#include <asm/ptrace.h>

#define ipipe_processor_id()   raw_smp_processor_id()

#define prepare_arch_switch(next)		\
do {						\
	ipipe_schedule_notify(current, next);	\
	local_irq_disable_hw();			\
} while(0)

#define task_hijacked(p)						\
	({ int x = !ipipe_root_domain_p; \
	__clear_bit(IPIPE_SYNC_FLAG, &ipipe_root_cpudom_var(status));	\
	local_irq_enable_hw(); x; })

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

#define __ipipe_check_platform()	do { } while(0)
#define __ipipe_init_platform()		do { } while(0)
#define __ipipe_enable_irq(irq)		irq_desc[irq].chip->enable(irq)
#define __ipipe_disable_irq(irq)	irq_desc[irq].chip->disable(irq)

#ifdef CONFIG_SMP
void __ipipe_hook_critical_ipi(struct ipipe_domain *ipd);
#else
#define __ipipe_hook_critical_ipi(ipd) do { } while(0)
#endif

#define __ipipe_disable_irqdesc(ipd, irq)	do { } while(0)

void __ipipe_enable_irqdesc(struct ipipe_domain *ipd, unsigned irq);

void __ipipe_enable_pipeline(void);

int __ipipe_handle_irq(struct pt_regs regs);

void __ipipe_do_critical_sync(unsigned irq, void *cookie);

extern int __ipipe_tick_irq;

DECLARE_PER_CPU(struct pt_regs, __ipipe_tick_regs);

#define __ipipe_call_root_xirq_handler(ipd,irq) \
   __asm__ __volatile__ ("pushfl\n\t" \
                         "pushl %%cs\n\t" \
                         "pushl $1f\n\t" \
	                 "pushl %%eax\n\t" \
                         "pushl %%fs\n\t" \
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
	                 "1: cli\n" \
			 : /* no output */ \
			 : "a" (~irq), "m" ((ipd)->irqs[irq].handler))

#define __ipipe_call_root_virq_handler(ipd,irq) \
   __asm__ __volatile__ ("pushfl\n\t" \
                         "pushl %%cs\n\t" \
                         "pushl $__virq_end\n\t" \
	                 "pushl $-1\n\t" \
                         "pushl %%fs\n\t" \
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
	                 "__virq_end: cli\n" \
			 : /* no output */ \
			 : /* no input */)

static inline unsigned long __ipipe_ffnz(unsigned long ul)
{
      __asm__("bsrl %1, %0":"=r"(ul)
      :	"r"(ul));
	return ul;
}

/*
 * When running handlers, enable hw interrupts for all domains but the
 * one heading the pipeline, so that IRQs can never be significantly
 * deferred for the latter.
 */
#define __ipipe_run_isr(ipd, irq)					\
	do {								\
		local_irq_enable_nohead(ipd);				\
		if (ipd == ipipe_root_domain) {				\
			if (likely(!ipipe_virtual_irq_p(irq))) {	\
				struct pt_regs *old_regs;		\
				old_regs = set_irq_regs(&__raw_get_cpu_var(__ipipe_tick_regs)); \
				__ipipe_call_root_xirq_handler(ipd,irq); \
				set_irq_regs(old_regs);			\
			} else {					\
				irq_enter();				\
				__ipipe_call_root_virq_handler(ipd,irq); \
				irq_exit();				\
				__ipipe_finalize_root_virq_handler();	\
			}						\
		} else {						\
			__clear_bit(IPIPE_SYNC_FLAG, &ipipe_cpudom_var(ipd, status)); \
			ipd->irqs[irq].handler(irq, ipd->irqs[irq].cookie); \
			__set_bit(IPIPE_SYNC_FLAG, &ipipe_cpudom_var(ipd, status)); \
		}							\
		local_irq_disable_nohead(ipd);				\
	} while(0)

#endif /* __ASSEMBLY__ */

#define __ipipe_syscall_watched_p(p, sc)	\
	(((p)->flags & PF_EVNOTIFY) || (unsigned long)sc >= NR_syscalls)

int __ipipe_check_lapic(void);

int __ipipe_check_tickdev(const char *devname);

#else /* !CONFIG_IPIPE */

#define task_hijacked(p)	0

#endif /* CONFIG_IPIPE */

#endif	/* !__I386_IPIPE_H */
