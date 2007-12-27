/*   -*- linux-c -*-
 *   include/asm-x86/ipipe_64.h
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

#ifndef __X86_IPIPE_64_H
#define __X86_IPIPE_64_H

#ifdef CONFIG_IPIPE

#ifndef __ASSEMBLY__

#include <asm/ptrace.h>
#include <asm/irq.h>
#include <linux/cpumask.h>
#include <linux/list.h>
#include <linux/ipipe_percpu.h>
#ifdef CONFIG_SMP
#include <asm/mpspec.h>
#include <linux/thread_info.h>
#endif

/*
 * The logical processor id is read from the PDA, so this is always
 * safe, regardless of the underlying stack.
 */
#define ipipe_processor_id()	raw_smp_processor_id()

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

#define ipipe_read_tsc(t)  do {		\
	unsigned int __a,__d;			\
	asm volatile("rdtsc" : "=a" (__a), "=d" (__d)); \
	(t) = ((unsigned long)__a) | (((unsigned long)__d)<<32); \
} while(0)

extern unsigned cpu_khz;
#define ipipe_cpu_freq() ({ unsigned long __freq = (1000UL * cpu_khz); __freq; })
#define ipipe_tsc2ns(t)	(((t) * 1000UL) / (ipipe_cpu_freq() / 1000000UL))
#define ipipe_tsc2us(t)	((t) / (ipipe_cpu_freq() / 1000000UL))

/*
 * The following interface will be deprecated once generic clockevents
 * are supported by this architecture, at which point
 * ipipe_request_tickdev() should be used instead.
 */
extern unsigned long __ipipe_apic_timer_freq;

/* Private interface -- Internal use only */

#define __ipipe_check_platform()	do { } while(0)
#define __ipipe_init_platform()		do { } while(0)
#define __ipipe_enable_irq(irq)		irq_desc[irq].chip->enable(irq)
#define __ipipe_disable_irq(irq)	irq_desc[irq].chip->disable(irq)

#ifdef CONFIG_SMP
void __ipipe_hook_critical_ipi(struct ipipe_domain *ipd);
#else
#define __ipipe_hook_critical_ipi(ipd)		do { } while(0)
#endif

#define __ipipe_disable_irqdesc(ipd, irq)	do { } while(0)

void __ipipe_enable_irqdesc(struct ipipe_domain *ipd,
			    unsigned irq);

void __ipipe_enable_pipeline(void);

int __ipipe_handle_irq(struct pt_regs *regs);

void __ipipe_do_critical_sync(unsigned irq, void *cookie);

void __ipipe_serial_debug(const char *fmt, ...);

extern int __ipipe_tick_irq;

DECLARE_PER_CPU(struct pt_regs, __ipipe_tick_regs);

unsigned __ipipe_get_irq_vector(int irq);

asmlinkage void __ipipe_root_xirq_thunk(unsigned irq,
					void (*handler)(unsigned irq, void *cookie),
					struct pt_regs *regs);

asmlinkage void __ipipe_root_virq_thunk(unsigned irq,
					void *cookie,
					void (*handler)(unsigned irq, void *cookie));

static inline unsigned long __ipipe_ffnz(unsigned long ul)
{
      __asm__("bsrq %1, %0":"=r"(ul)
	      :	"rm"(ul));
      return ul;
}

struct irq_desc;

void __ipipe_ack_edge_irq(unsigned irq, struct irq_desc *desc);

void __ipipe_end_edge_irq(unsigned irq, struct irq_desc *desc);

/*
 * When running handlers, enable hw interrupts for all domains but the
 * one heading the pipeline, so that IRQs can never be significantly
 * deferred for the latter.
 */
#define __ipipe_run_isr(ipd, irq)					\
	do {								\
		local_irq_enable_nohead(ipd);				\
		if (ipd == ipipe_root_domain) {				\
			if (likely(!ipipe_virtual_irq_p(irq)))		\
				__ipipe_root_xirq_thunk(~__ipipe_get_irq_vector(irq),	\
							(ipd)->irqs[irq].handler,	\
							&__raw_get_cpu_var(__ipipe_tick_regs)); \
			else						\
				__ipipe_root_virq_thunk(irq, \
							(ipd)->irqs[irq].cookie, \
							(ipd)->irqs[irq].handler); \
		} else {						\
			__clear_bit(IPIPE_SYNC_FLAG, &ipipe_cpudom_var(ipd, status)); \
			ipd->irqs[irq].handler(irq, ipd->irqs[irq].cookie); \
			__set_bit(IPIPE_SYNC_FLAG, &ipipe_cpudom_var(ipd, status)); \
		}							\
		local_irq_disable_nohead(ipd);				\
	} while(0)

#endif /* __ASSEMBLY__ */

#define __ipipe_syscall_watched_p(p, sc)	\
	(((p)->flags & PF_EVNOTIFY) || (unsigned long)sc >= __NR_syscalls)

int __ipipe_check_lapic(void);

int __ipipe_check_tickdev(const char *devname);

#else /* !CONFIG_IPIPE */

#define task_hijacked(p)	0

#endif /* CONFIG_IPIPE */

#define ipipe_update_tick_evtdev(evtdev)	do { } while (0)

#endif	/* !__X86_IPIPE_64_H */
