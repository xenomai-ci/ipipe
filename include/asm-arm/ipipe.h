/* -*- linux-c -*-
 * include/asm-arm/ipipe.h
 *
 * Copyright (C) 2002-2005 Philippe Gerum.
 * Copyright (C) 2005 Stelian Pop.
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

#ifndef __ARM_IPIPE_H
#define __ARM_IPIPE_H

#ifdef CONFIG_IPIPE

#include <asm/irq.h>
#include <asm/percpu.h>

#define IPIPE_ARCH_STRING	"1.7-04"
#define IPIPE_MAJOR_NUMBER	1
#define IPIPE_MINOR_NUMBER	7
#define IPIPE_PATCH_NUMBER	4

#define IPIPE_NR_XIRQS		NR_IRQS
#define IPIPE_IRQ_ISHIFT	5	/* 25 for 32bits arch. */

#ifdef CONFIG_SMP
#error "I-pipe/arm: SMP not yet implemented"
#define ipipe_processor_id()	(current_thread_info()->cpu)
#else /* !CONFIG_SMP */
#define ipipe_processor_id()	0
#endif	/* CONFIG_SMP */

#define smp_processor_id_hw() ipipe_processor_id()

#define prepare_arch_switch(next)				\
do {								\
	ipipe_schedule_notify(current, next);			\
} while(0)

#define task_hijacked(p)						\
	({								\
		int __x__ = ipipe_current_domain != ipipe_root_domain;	\
		/* We would need to clear the SYNC flag for the root domain */ \
		/* over the current processor in SMP mode. */		\
		 __x__;				\
	})

/* ARM traps */
#define IPIPE_TRAP_ACCESS	 0	/* Data or instruction access exception */
#define IPIPE_TRAP_SECTION	 1	/* Section fault */
#define IPIPE_TRAP_DABT		 2	/* Generic data abort */
#define IPIPE_TRAP_UNKNOWN	 3	/* Unknown exception */
#define IPIPE_TRAP_BREAK	 4	/* Instruction breakpoint */
#define IPIPE_TRAP_FPU		 5	/* Floating point exception */
#define IPIPE_TRAP_VFP		 6	/* VFP floating point exception */
#define IPIPE_TRAP_UNDEFINSTR	 7	/* Undefined instruction */
#define IPIPE_NR_FAULTS		 8

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

extern unsigned long arm_return_addr(int level);

#define BROKEN_BUILTIN_RETURN_ADDRESS
#define __BUILTIN_RETURN_ADDRESS0 arm_return_addr(0)
#define __BUILTIN_RETURN_ADDRESS1 arm_return_addr(1)


struct ipipe_domain;

#define IPIPE_TSC_TYPE_NONE        0
#define IPIPE_TSC_TYPE_FREERUNNING 1
#define IPIPE_TSC_TYPE_DECREMENTER 2

struct __ipipe_tscinfo {
        unsigned type;
        union {
                struct {
                        unsigned *counter; /* Hw counter physical address */
                        unsigned mask; /* Significant bits in the hw counter. */
                        unsigned long long *tsc; /* 64 bits tsc value. */
                } fr;
                struct {
                } dec;
        } u;
};

struct ipipe_sysinfo {

	int ncpus;		/* Number of CPUs on board */
	u64 cpufreq;		/* CPU frequency (in Hz) */

	/* Arch-dependent block */

	struct {
		unsigned tmirq;	/* Timer tick IRQ */
		u64 tmfreq;	/* Timer frequency */
                struct __ipipe_tscinfo tsc; /* exported data for u.s. tsc */
	} archdep;
};

DECLARE_PER_CPU(struct mm_struct *,ipipe_active_mm);
/* arch specific stuff */
extern void *__ipipe_tsc_area;
extern int __ipipe_mach_timerint;
extern int __ipipe_mach_timerstolen;
extern unsigned int __ipipe_mach_ticks_per_jiffy;
extern void __ipipe_mach_acktimer(void);
extern unsigned long long __ipipe_mach_get_tsc(void);
extern void __ipipe_mach_set_dec(unsigned long);
extern void __ipipe_mach_release_timer(void);
extern unsigned long __ipipe_mach_get_dec(void);
extern void __ipipe_mach_demux_irq(unsigned irq, struct pt_regs *regs);
void __ipipe_mach_get_tscinfo(struct __ipipe_tscinfo *info);

#define ipipe_read_tsc(t)		do { t = __ipipe_mach_get_tsc(); } while (0)
#define __ipipe_read_timebase()		__ipipe_mach_get_tsc()

#define ipipe_cpu_freq()	(HZ * __ipipe_mach_ticks_per_jiffy)
#define ipipe_tsc2ns(t) \
({ \
	unsigned long long delta = (t)*1000; \
	do_div(delta, ipipe_cpu_freq() / 1000000 + 1); \
	(unsigned long)delta; \
})
#define ipipe_tsc2us(t) \
({ \
	unsigned long long delta = (t); \
	do_div(delta, ipipe_cpu_freq() / 1000000 + 1); \
	(unsigned long)delta; \
})

/* Private interface -- Internal use only */

#define __ipipe_check_platform()	do { } while(0)

void __ipipe_init_platform(void);

#define __ipipe_enable_irq(irq)	irq_desc[irq].chip->enable(irq)

#define __ipipe_disable_irq(irq)	irq_desc[irq].chip->disable(irq)

#define __ipipe_hook_critical_ipi(ipd) do { } while(0)

void __ipipe_enable_irqdesc(unsigned irq);

void __ipipe_enable_pipeline(void);

void __ipipe_do_IRQ(int irq,
		    struct pt_regs *regs);

void __ipipe_do_timer(int irq,
		      struct pt_regs *regs);

void __ipipe_do_critical_sync(unsigned irq,
			      void *cookie);

extern unsigned long __ipipe_decr_ticks;

extern unsigned long long __ipipe_decr_next[];

extern struct pt_regs __ipipe_tick_regs[];

void __ipipe_handle_irq(int irq,
			struct pt_regs *regs);

#define __ipipe_tick_irq	ipipe_timerint

static inline unsigned long __ipipe_ffnz(unsigned long ul)
{
	return ffs(ul) - 1;
}

/* When running handlers, enable hw interrupts for all domains but the
 * one heading the pipeline, so that IRQs can never be significantly
 * deferred for the latter. */
#define __ipipe_run_isr(ipd, irq, cpuid)                                \
do {                                                                    \
	local_irq_enable_nohead(ipd);                                   \
	if (ipd == ipipe_root_domain) {                                 \
                if (likely(!ipipe_virtual_irq_p(irq)))                  \
                        ((void (*)(unsigned, struct pt_regs *))         \
                         ipd->irqs[irq].handler) (irq,                  \
                                                  __ipipe_tick_regs + cpuid); \
                else {                                                  \
                        irq_enter();                                    \
                        ipd->irqs[irq].handler(irq,ipd->irqs[irq].cookie); \
                        irq_exit();                                     \
                }                                                       \
	} else {							\
		__clear_bit(IPIPE_SYNC_FLAG, &cpudata->status);		\
		ipd->irqs[irq].handler(irq,ipd->irqs[irq].cookie);	\
		__set_bit(IPIPE_SYNC_FLAG, &cpudata->status);		\
	}								\
	local_irq_disable_nohead(ipd);					\
} while(0)

#define __ipipe_syscall_watched_p(p, sc)	\
	(((p)->flags & PF_EVNOTIFY) || (unsigned long)sc >= __ARM_NR_BASE + 64)

#else /* !CONFIG_IPIPE */

#define task_hijacked(p)		0

#define smp_processor_id_hw()		smp_processor_id()

#endif /* CONFIG_IPIPE */

#endif	/* !__ARM_IPIPE_H */
