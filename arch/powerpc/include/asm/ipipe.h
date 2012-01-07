/*
 *   include/asm-powerpc/ipipe.h
 *
 *   I-pipe 32/64bit merge - Copyright (C) 2007 Philippe Gerum.
 *   I-pipe PA6T support - Copyright (C) 2007 Philippe Gerum.
 *   I-pipe 64-bit PowerPC port - Copyright (C) 2005 Heikki Lindholm.
 *   I-pipe PowerPC support - Copyright (C) 2002-2005 Philippe Gerum.
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

#ifndef __ASM_POWERPC_IPIPE_H
#define __ASM_POWERPC_IPIPE_H

#ifdef CONFIG_IPIPE

#include <asm/ptrace.h>
#include <asm/hw_irq.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/time.h>
#include <linux/ipipe_domain.h>
#include <linux/irq.h>
#include <linux/list.h>
#include <linux/cpumask.h>
#include <linux/cache.h>
#include <linux/threads.h>

#ifdef CONFIG_PPC64
#ifdef CONFIG_PPC_ISERIES
#error "I-pipe: IBM I-series not supported, sorry"
#endif
#include <asm/paca.h>
#endif

#define IPIPE_CORE_RELEASE	1

struct ipipe_domain;

#ifdef CONFIG_DEBUGGER
extern cpumask_t __ipipe_dbrk_pending;
#endif

#define ipipe_mm_switch_protect(flags)					\
	do {								\
		__mmactivate_head();					\
		barrier();						\
		(void)(flags);						\
	} while(0)

#define ipipe_mm_switch_unprotect(flags)				\
	do {								\
		barrier();						\
		__mmactivate_tail();					\
		(void)(flags);						\
	} while(0)

#define __ipipe_hrtimer_irq	IPIPE_TIMER_VIRQ
#define __ipipe_hrtimer_freq	ppc_tb_freq
#define __ipipe_hrclock_freq	__ipipe_hrtimer_freq
#define __ipipe_cpu_freq	ppc_proc_freq

#ifdef CONFIG_PPC64
#define ipipe_read_tsc(t)	(t = mftb())
#define ipipe_tsc2ns(t)		(((t) * 1000UL) / (__ipipe_hrclock_freq / 1000000UL))
#define ipipe_tsc2us(t)		((t) / (__ipipe_hrclock_freq / 1000000UL))
#else /* CONFIG_PPC32 */
#define ipipe_read_tsc(t)					\
	({							\
		unsigned long __tbu;				\
		__asm__ __volatile__ ("1: mftbu %0\n"		\
				      "mftb %1\n"		\
				      "mftbu %2\n"		\
				      "cmpw %2,%0\n"		\
				      "bne- 1b\n"		\
				      :"=r" (((unsigned long *)&t)[0]),	\
				       "=r" (((unsigned long *)&t)[1]),	\
				       "=r" (__tbu));			\
		t;							\
	})

#define ipipe_tsc2ns(t)	\
	((((unsigned long)(t)) * 1000) / (__ipipe_hrclock_freq / 1000000))

#define ipipe_tsc2us(t)						\
	({							\
		unsigned long long delta = (t);			\
		do_div(delta, __ipipe_hrclock_freq/1000000+1);	\
		(unsigned long)delta;				\
	})
#endif /* CONFIG_PPC32 */

/* Private interface -- Internal use only */

#define __ipipe_check_platform()		do { } while(0)
#define __ipipe_enable_irq(irq)			enable_irq(irq)
#define __ipipe_disable_irq(irq)		disable_irq(irq)
#define __ipipe_enable_irqdesc(ipd, irq)	do { } while(0)
#define __ipipe_disable_irqdesc(ipd, irq)	do { } while(0)

void __ipipe_init_platform(void);

void __ipipe_enable_pipeline(void);

void __ipipe_end_irq(unsigned irq);

static inline int __ipipe_check_tickdev(const char *devname)
{
	return 1;
}

#ifdef CONFIG_SMP
struct ipipe_ipi_struct {
	volatile unsigned long value;
} ____cacheline_aligned;

void __ipipe_hook_critical_ipi(struct ipipe_domain *ipd);

void __ipipe_register_ipi(unsigned int irq);
#else
#define __ipipe_hook_critical_ipi(ipd)	do { } while(0)
#endif /* CONFIG_SMP */

void __ipipe_dispatch_irq(unsigned int irq, int ackit);

static inline void __ipipe_handle_irq(unsigned int irq, struct pt_regs *regs)
{
	/* NULL regs means software-triggered, no ack needed. */
	__ipipe_dispatch_irq(irq, regs != NULL);
}

static inline void ipipe_handle_chained_irq(unsigned int irq)
{
	struct pt_regs regs;	/* dummy */

	ipipe_trace_irq_entry(irq);
	__ipipe_handle_irq(irq, &regs);
	ipipe_trace_irq_exit(irq);
}

struct irq_desc;
void __ipipe_ack_level_irq(unsigned irq, struct irq_desc *desc);
void __ipipe_end_level_irq(unsigned irq, struct irq_desc *desc);
void __ipipe_ack_edge_irq(unsigned irq, struct irq_desc *desc);
void __ipipe_end_edge_irq(unsigned irq, struct irq_desc *desc);

#ifdef CONFIG_IPIPE_DEBUG
void __ipipe_serial_debug(const char *fmt, ...);
#else
#define __ipipe_serial_debug(fmt, args...)	do { } while (0)
#endif

#define __ipipe_tick_irq	IPIPE_TIMER_VIRQ

static inline unsigned long __ipipe_ffnz(unsigned long ul)
{
#ifdef CONFIG_PPC64
	__asm__ __volatile__("cntlzd %0, %1":"=r"(ul):"r"(ul & (-ul)));
	return 63 - ul;
#else
	__asm__ __volatile__("cntlzw %0, %1":"=r"(ul):"r"(ul & (-ul)));
	return 31 - ul;
#endif
}

#define __ipipe_syscall_watched_p(p, sc)	\
	(ipipe_notifier_enabled_p(p) || (unsigned long)sc >= NR_syscalls)

#define __ipipe_root_tick_p(regs)	((regs)->msr & MSR_EE)

void handle_one_irq(unsigned int irq);

void check_stack_overflow(void);

static inline void ipipe_pre_cascade_noeoi(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	chip->irq_mask(&desc->irq_data);
}

static inline void ipipe_post_cascade_noeoi(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	chip->irq_unmask(&desc->irq_data);
}

static inline void ipipe_pre_cascade_eoi(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	chip->irq_eoi(&desc->irq_data); /* EOI will mask too. */
}

static inline void ipipe_post_cascade_eoi(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	chip->irq_unmask(&desc->irq_data);
}

static inline void ipipe_mute_pic(void) { }

static inline void ipipe_unmute_pic(void) { }

static inline void ipipe_notify_root_preemption(void) { }

#else /* !CONFIG_IPIPE */

#include <linux/interrupt.h>

#define ipipe_mm_switch_protect(flags)		do { (void)(flags); } while(0)
#define ipipe_mm_switch_unprotect(flags)	do { (void)(flags); } while(0)
#define	ipipe_pre_cascade_noeoi(desc)		do { } while (0)
#define	ipipe_post_cascade_noeoi(desc)		do { } while (0)
#define	ipipe_pre_cascade_eoi(desc)		do { } while (0)

static inline void ipipe_post_cascade_eoi(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	chip->irq_eoi(&desc->irq_data);
}

#endif /* !CONFIG_IPIPE */

#define ipipe_update_tick_evtdev(evtdev)	do { } while (0)

#endif /* !__ASM_POWERPC_IPIPE_H */
