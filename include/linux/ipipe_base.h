/* -*- linux-c -*-
 * include/linux/ipipe_base.h
 *
 * Copyright (C) 2002-2012 Philippe Gerum.
 *               2007 Jan Kiszka.
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

#ifndef __LINUX_IPIPE_BASE_H
#define __LINUX_IPIPE_BASE_H

#ifdef CONFIG_IPIPE

#include <asm/ipipe_base.h>

#define __bpl_up(x)		(((x)+(BITS_PER_LONG-1)) & ~(BITS_PER_LONG-1))
/* Number of virtual IRQs (must be a multiple of BITS_PER_LONG) */
#define IPIPE_NR_VIRQS		BITS_PER_LONG
/* First virtual IRQ # (must be aligned on BITS_PER_LONG) */
#define IPIPE_VIRQ_BASE		__bpl_up(IPIPE_NR_XIRQS)
/* Total number of IRQ slots */
#define IPIPE_NR_IRQS		(IPIPE_VIRQ_BASE+IPIPE_NR_VIRQS)

static inline int ipipe_virtual_irq_p(unsigned int irq)
{
	return irq >= IPIPE_VIRQ_BASE && irq < IPIPE_NR_IRQS;
}

#define IPIPE_IRQ_LOMAPSZ	(IPIPE_NR_IRQS / BITS_PER_LONG)
#if IPIPE_IRQ_LOMAPSZ > BITS_PER_LONG
/*
 * We need a 3-level mapping. This allows us to handle up to 32k IRQ
 * vectors on 32bit machines, 256k on 64bit ones.
 */
#define __IPIPE_3LEVEL_IRQMAP	1
#define IPIPE_IRQ_MDMAPSZ	(__bpl_up(IPIPE_IRQ_LOMAPSZ) / BITS_PER_LONG)
#else
/*
 * 2-level mapping is enough. This allows us to handle up to 1024 IRQ
 * vectors on 32bit machines, 4096 on 64bit ones.
 */
#define __IPIPE_2LEVEL_IRQMAP	1
#endif

/* Per-cpu pipeline status */
#define IPIPE_STALL_FLAG	0 /* interrupts (virtually) disabled. */
#define IPIPE_NOSTACK_FLAG	1 /* running on foreign stack. */
#define IPIPE_GUEST_FLAG	2 /* root entered guest mode. */
#define IPIPE_STALL_MASK	(1L << IPIPE_STALL_FLAG)
#define IPIPE_NOSTACK_MASK	(1L << IPIPE_NOSTACK_FLAG)
#define IPIPE_GUEST_MASK	(1L << IPIPE_GUEST_FLAG)

/* Interrupt control bits */
#define IPIPE_HANDLE_FLAG	0
#define IPIPE_STICKY_FLAG	1
#define IPIPE_LOCK_FLAG		2
#define IPIPE_HANDLE_MASK	(1 << IPIPE_HANDLE_FLAG)
#define IPIPE_STICKY_MASK	(1 << IPIPE_STICKY_FLAG)
#define IPIPE_LOCK_MASK		(1 << IPIPE_LOCK_FLAG)

#ifdef CONFIG_SMP
#define local_irq_save_hw_smp(flags)		local_irq_save_hw(flags)
#define local_irq_restore_hw_smp(flags)		local_irq_restore_hw(flags)
#else /* !CONFIG_SMP */
#define local_irq_save_hw_smp(flags)		do { (void)(flags); } while(0)
#define local_irq_restore_hw_smp(flags)		do { } while(0)
#endif /* CONFIG_SMP */

struct pt_regs;
struct kvm_vcpu;
struct ipipe_domain;

struct ipipe_trap_data {
	int exception;
	struct pt_regs *regs;
};

#define IPIPE_KEVT_SCHEDULE	0
#define IPIPE_KEVT_SIGWAKE	1
#define IPIPE_KEVT_SETSCHED	2
#define IPIPE_KEVT_EXIT		3
#define IPIPE_KEVT_CLEANUP	4
#define IPIPE_KEVT_HOSTRT	5

typedef void (*ipipe_irq_handler_t)(unsigned int irq,
				    void *cookie);

void __ipipe_init_early(void);

void __ipipe_init(void);

#ifdef CONFIG_PROC_FS
void __ipipe_init_proc(void);
#ifdef CONFIG_IPIPE_TRACE
void __ipipe_init_tracer(void);
#else /* !CONFIG_IPIPE_TRACE */
static inline void __ipipe_init_tracer(void) { }
#endif /* CONFIG_IPIPE_TRACE */
#else	/* !CONFIG_PROC_FS */
static inline void __ipipe_init_proc(void) { }
#endif	/* CONFIG_PROC_FS */

void ipipe_unstall_root(void);

void ipipe_restore_root(unsigned long x);

void __ipipe_restore_root_nosync(unsigned long x);

static inline void ipipe_restore_root_nosync(unsigned long x)
{
	unsigned long flags;
	local_irq_save_hw_smp(flags);
	__ipipe_restore_root_nosync(x);
	local_irq_restore_hw_smp(flags);
}

void __ipipe_dispatch_irq(unsigned int irq, int ackit);

void __ipipe_do_sync_stage(void);

void __ipipe_do_sync_pipeline(struct ipipe_domain *top);

void __ipipe_lock_irq(unsigned int irq);

void __ipipe_unlock_irq(unsigned int irq);

void __ipipe_dispatch_irq_fast(unsigned int irq);

void __ipipe_dispatch_irq_fast_nocheck(unsigned int irq);

void __ipipe_do_critical_sync(unsigned int irq, void *cookie);

static inline void __ipipe_idle(void)
{
	ipipe_unstall_root();
}

#ifndef __ipipe_sync_check
#define __ipipe_sync_check	1
#endif

static inline void __ipipe_sync_stage(void)
{
	if (likely(__ipipe_sync_check))
		__ipipe_do_sync_stage();
}

#ifndef __ipipe_do_root_xirq
#define __ipipe_do_root_xirq(ipd, irq)			\
	(ipd)->irqs[irq].handler(irq, (ipd)->irqs[irq].cookie)
#endif

#ifndef __ipipe_check_root_resched
#ifdef CONFIG_PREEMPT
#define __ipipe_check_root_resched()	\
	(preempt_count() == 0 && need_resched())
#else
#define __ipipe_check_root_resched()	0
#endif
#endif

#ifndef __ipipe_run_irqtail
#define __ipipe_run_irqtail(irq) do { } while(0)
#endif

#ifdef CONFIG_SMP
int __ipipe_set_irq_affinity(unsigned int irq, cpumask_t cpumask);
int __ipipe_send_ipi(unsigned int ipi, cpumask_t cpumask);
#endif /* CONFIG_SMP */

void __ipipe_flush_printk(unsigned int irq, void *cookie);

void __ipipe_pin_range_globally(unsigned long start,
				unsigned long end);

#define __ipipe_preempt_disable(flags)		\
	do {					\
		local_irq_save_hw(flags);	\
		if (__ipipe_root_p)		\
			preempt_disable();	\
	} while (0)

#define __ipipe_preempt_enable(flags)			\
	do {						\
		if (__ipipe_root_p) {			\
			preempt_enable_no_resched();	\
			local_irq_restore_hw(flags);	\
			preempt_check_resched();	\
		} else					\
			local_irq_restore_hw(flags);	\
	} while (0)

#define __ipipe_get_cpu(flags)	({ __ipipe_preempt_disable(flags); ipipe_processor_id(); })
#define __ipipe_put_cpu(flags)	__ipipe_preempt_enable(flags)
 
int __ipipe_notify_syscall(struct pt_regs *regs);

int __ipipe_notify_trap(int exception, struct pt_regs *regs);

int __ipipe_notify_kevent(int event, void *data);

#define __ipipe_report_trap(exception, regs)				\
	__ipipe_notify_trap(exception, regs)

#define __ipipe_report_sigwake(p)					\
	do {								\
		if (ipipe_notifier_enabled_p(p))			\
			__ipipe_notify_kevent(IPIPE_KEVT_SIGWAKE, p);	\
	} while (0)

#define __ipipe_report_exit(p)						\
	do {								\
		if (ipipe_notifier_enabled_p(p))			\
			__ipipe_notify_kevent(IPIPE_KEVT_EXIT, p);	\
	} while (0)

#define __ipipe_report_setsched(p)					\
	do {								\
		if (ipipe_notifier_enabled_p(p))			\
			__ipipe_notify_kevent(IPIPE_KEVT_SETSCHED, p); \
	} while (0)

#define __ipipe_report_schedule(prev, next)				\
do {									\
	if ((ipipe_notifier_enabled_p(next) ||				\
	     ipipe_notifier_enabled_p(prev)))				\
		__ipipe_notify_kevent(IPIPE_KEVT_SCHEDULE, next);	\
} while (0)

#define __ipipe_report_cleanup(mm)					\
	__ipipe_notify_kevent(IPIPE_KEVT_CLEANUP, mm)

/* KVM-side calls. */
void __ipipe_register_guest(struct kvm_vcpu *vcpu);
void __ipipe_enter_guest(void);
void __ipipe_exit_guest(void);
void __ipipe_handle_guest_preemption(struct kvm_vcpu *vcpu);

/* Client-side call through ipipe_notify_root_preemption(). */
void __ipipe_notify_guest_preemption(void);

#define local_irq_enable_hw_cond()		local_irq_enable_hw()
#define local_irq_disable_hw_cond()		local_irq_disable_hw()
#define local_irq_save_hw_cond(flags)		local_irq_save_hw(flags)
#define local_irq_restore_hw_cond(flags)	local_irq_restore_hw(flags)

#define local_irq_save_full(vflags, rflags)		\
	do {						\
		local_irq_save(vflags);			\
		local_irq_save_hw(rflags);		\
	} while(0)

#define local_irq_restore_full(vflags, rflags)		\
	do {						\
		local_irq_restore_hw(rflags);		\
		local_irq_restore(vflags);		\
	} while(0)

#ifdef CONFIG_IPIPE_DEBUG_CONTEXT
void ipipe_root_only(void);
#else /* !CONFIG_IPIPE_DEBUG_CONTEXT */
static inline void ipipe_root_only(void) { }
#endif /* !CONFIG_IPIPE_DEBUG_CONTEXT */

struct ipipe_task_info {
	unsigned long flags;
};

#ifdef CONFIG_IPIPE_LEGACY

#define IPIPE_FIRST_EVENT	IPIPE_NR_FAULTS
#define IPIPE_EVENT_SCHEDULE	IPIPE_FIRST_EVENT
#define IPIPE_EVENT_SIGWAKE	(IPIPE_FIRST_EVENT + 1)
#define IPIPE_EVENT_SETSCHED	(IPIPE_FIRST_EVENT + 2)
#define IPIPE_EVENT_EXIT	(IPIPE_FIRST_EVENT + 3)
#define IPIPE_EVENT_CLEANUP	(IPIPE_FIRST_EVENT + 4)
#define IPIPE_EVENT_HOSTRT	(IPIPE_FIRST_EVENT + 5)
#define IPIPE_EVENT_SYSCALL	(IPIPE_FIRST_EVENT + 6)
#define IPIPE_LAST_EVENT	IPIPE_EVENT_SYSCALL
#define IPIPE_NR_EVENTS		(IPIPE_LAST_EVENT + 1)

typedef int (*ipipe_event_handler_t)(unsigned int event,
				     struct ipipe_domain *from,
				     void *data);
struct ipipe_legacy_context {
	unsigned int domid;
	int priority;
	void *pdd;
	ipipe_event_handler_t handlers[IPIPE_NR_EVENTS];
};

#define __ipipe_init_taskinfo(p)			\
	do {						\
		__ipipe_clear_taskflags(p);		\
		memset(p->ptd, 0, sizeof(p->ptd));	\
	} while (0)

#else /* !CONFIG_IPIPE_LEGACY */

struct ipipe_legacy_context {
};

#define __ipipe_init_taskinfo(p)			\
	do {						\
		__ipipe_clear_taskflags(p);		\
	} while (0)

#endif /* !CONFIG_IPIPE_LEGACY */

#define __ipipe_clear_taskflags(p)	\
	do {				\
		(p)->ipipe.flags = 0;	\
	} while (0)

#else /* !CONFIG_IPIPE */

struct task_struct;
struct mm_struct;

struct ipipe_task_info {
};

static inline void __ipipe_init_early(void) { }

static inline void __ipipe_init(void) { }

static inline void __ipipe_init_proc(void) { }

static inline void __ipipe_idle(void) { }

static inline void __ipipe_report_sigwake(struct task_struct *p) { }

static inline void __ipipe_report_setsched(struct task_struct *p) { }

static inline void __ipipe_report_exit(struct task_struct *p) { }

static inline void __ipipe_report_cleanup(struct mm_struct *mm) { }

#define __ipipe_report_trap(exception, regs)  0

static inline void __ipipe_init_taskinfo(struct task_struct *p) { }

static inline void __ipipe_clear_taskflags(struct task_struct *p) { }

static inline void __ipipe_pin_range_globally(unsigned long start,
					      unsigned long end)
{ }

#define __ipipe_preempt_disable(flags)	\
	do {				\
		preempt_disable();	\
		(void)(flags);		\
	} while (0)
#define __ipipe_preempt_enable(flags)	preempt_enable()

#define __ipipe_get_cpu(flags)		({ (void)(flags); get_cpu(); })
#define __ipipe_put_cpu(flags)		\
	do {				\
		(void)(flags);		\
		put_cpu();		\
	} while (0)
	
static inline void ipipe_root_only(void) { }

#define local_irq_enable_hw_cond()		do { } while(0)
#define local_irq_disable_hw_cond()		do { } while(0)
#define local_irq_save_hw_cond(flags)		do { (void)(flags); } while(0)
#define local_irq_restore_hw_cond(flags)	do { } while(0)
#define local_irq_save_hw_smp(flags)		do { (void)(flags); } while(0)
#define local_irq_restore_hw_smp(flags)		do { } while(0)

#define local_irq_save_full(vflags, rflags)	do { (void)(vflags); local_irq_save(rflags); } while(0)
#define local_irq_restore_full(vflags, rflags)	do { (void)(vflags); local_irq_restore(rflags); } while(0)

#endif	/* CONFIG_IPIPE */

#endif	/* !__LINUX_IPIPE_BASE_H */
