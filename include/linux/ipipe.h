/* -*- linux-c -*-
 * include/linux/ipipe.h
 *
 * Copyright (C) 2002-2005 Philippe Gerum.
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

#ifndef __LINUX_IPIPE_H
#define __LINUX_IPIPE_H

#include <linux/spinlock.h>
#include <linux/cache.h>
#include <linux/percpu.h>
#include <linux/mutex.h>
#include <linux/linkage.h>
#include <linux/irq.h>
#include <asm/ipipe.h>

#ifdef CONFIG_IPIPE

#define IPIPE_VERSION_STRING	IPIPE_ARCH_STRING
#define IPIPE_RELEASE_NUMBER	((IPIPE_MAJOR_NUMBER << 16) | \
				 (IPIPE_MINOR_NUMBER <<  8) | \
				 (IPIPE_PATCH_NUMBER))

#ifndef BROKEN_BUILTIN_RETURN_ADDRESS
#define __BUILTIN_RETURN_ADDRESS0 ((unsigned long)__builtin_return_address(0))
#define __BUILTIN_RETURN_ADDRESS1 ((unsigned long)__builtin_return_address(1))
#endif /* !BUILTIN_RETURN_ADDRESS */

#define IPIPE_ROOT_PRIO		100
#define IPIPE_ROOT_ID		0
#define IPIPE_ROOT_NPTDKEYS	4	/* Must be <= BITS_PER_LONG */

#define IPIPE_RESET_TIMER	0x1
#define IPIPE_GRAB_TIMER	0x2

/* Global domain flags */
#define IPIPE_SPRINTK_FLAG	0	/* Synchronous printk() allowed */
#define IPIPE_AHEAD_FLAG	1	/* Domain always heads the pipeline */

/* Per-cpu pipeline status */
#define IPIPE_STALL_FLAG	0	/* Stalls a pipeline stage -- guaranteed at bit #0 */
#define IPIPE_SYNC_FLAG		1	/* The interrupt syncer is running for the domain */
#define IPIPE_NOSTACK_FLAG	2	/* Domain currently runs on a foreign stack */

#define IPIPE_SYNC_MASK		(1 << IPIPE_SYNC_FLAG)

/* Interrupt control bits */
#define IPIPE_HANDLE_FLAG	0
#define IPIPE_PASS_FLAG		1
#define IPIPE_ENABLE_FLAG	2
#define IPIPE_DYNAMIC_FLAG	IPIPE_HANDLE_FLAG
#define IPIPE_STICKY_FLAG	3
#define IPIPE_SYSTEM_FLAG	4
#define IPIPE_LOCK_FLAG		5
#define IPIPE_WIRED_FLAG	6
#define IPIPE_EXCLUSIVE_FLAG	7

#define IPIPE_HANDLE_MASK	(1 << IPIPE_HANDLE_FLAG)
#define IPIPE_PASS_MASK		(1 << IPIPE_PASS_FLAG)
#define IPIPE_ENABLE_MASK	(1 << IPIPE_ENABLE_FLAG)
#define IPIPE_DYNAMIC_MASK	IPIPE_HANDLE_MASK
#define IPIPE_STICKY_MASK	(1 << IPIPE_STICKY_FLAG)
#define IPIPE_SYSTEM_MASK	(1 << IPIPE_SYSTEM_FLAG)
#define IPIPE_LOCK_MASK		(1 << IPIPE_LOCK_FLAG)
#define IPIPE_WIRED_MASK	(1 << IPIPE_WIRED_FLAG)
#define IPIPE_EXCLUSIVE_MASK	(1 << IPIPE_EXCLUSIVE_FLAG)

#define IPIPE_DEFAULT_MASK	(IPIPE_HANDLE_MASK|IPIPE_PASS_MASK)
#define IPIPE_STDROOT_MASK	(IPIPE_HANDLE_MASK|IPIPE_PASS_MASK|IPIPE_SYSTEM_MASK)

#define IPIPE_EVENT_SELF        0x80000000

/* Number of virtual IRQs */
#define IPIPE_NR_VIRQS		BITS_PER_LONG
/* First virtual IRQ # */
#define IPIPE_VIRQ_BASE		(((IPIPE_NR_XIRQS + BITS_PER_LONG - 1) / BITS_PER_LONG) * BITS_PER_LONG)
/* Total number of IRQ slots */
#define IPIPE_NR_IRQS		(IPIPE_VIRQ_BASE + IPIPE_NR_VIRQS)
/* Number of indirect words needed to map the whole IRQ space. */
#define IPIPE_IRQ_IWORDS	((IPIPE_NR_IRQS + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define IPIPE_IRQ_IMASK		(BITS_PER_LONG - 1)
#define IPIPE_IRQMASK_ANY	(~0L)
#define IPIPE_IRQMASK_VIRT	(IPIPE_IRQMASK_ANY << (IPIPE_VIRQ_BASE / BITS_PER_LONG))

#ifdef CONFIG_SMP

#define IPIPE_NR_CPUS		NR_CPUS
#define ipipe_declare_cpuid	int cpuid
#define ipipe_load_cpuid()	do { \
					cpuid = ipipe_processor_id();	\
				} while(0)
#define ipipe_lock_cpu(flags)	do { \
					local_irq_save_hw(flags); \
					cpuid = ipipe_processor_id(); \
				} while(0)
#define ipipe_unlock_cpu(flags)	local_irq_restore_hw(flags)
#define ipipe_get_cpu(flags)	ipipe_lock_cpu(flags)
#define ipipe_put_cpu(flags)	ipipe_unlock_cpu(flags)
#define ipipe_current_domain	per_cpu(ipipe_percpu_domain, ipipe_processor_id())

#else /* !CONFIG_SMP */

#define IPIPE_NR_CPUS		1
#define ipipe_declare_cpuid	const int cpuid = 0
#define ipipe_load_cpuid()	do { } while(0)
#define ipipe_lock_cpu(flags)	local_irq_save_hw(flags)
#define ipipe_unlock_cpu(flags)	local_irq_restore_hw(flags)
#define ipipe_get_cpu(flags)	do { (void)(flags); } while(0)
#define ipipe_put_cpu(flags)	do { } while(0)
#define ipipe_current_domain	per_cpu(ipipe_percpu_domain, 0)

#endif /* CONFIG_SMP */

#define ipipe_virtual_irq_p(irq)	((irq) >= IPIPE_VIRQ_BASE && \
					 (irq) < IPIPE_NR_IRQS)

typedef void (*ipipe_irq_handler_t)(unsigned irq,
				    void *cookie);

#define IPIPE_SAME_HANDLER	((ipipe_irq_handler_t)(-1))

typedef int (*ipipe_irq_ackfn_t)(unsigned irq);

typedef int (*ipipe_event_handler_t)(unsigned event,
				     struct ipipe_domain *from,
				     void *data);
struct ipipe_domain {

	struct list_head p_link;	/* Link in pipeline */

	struct ipcpudata {
		unsigned long status;
		unsigned long irq_pending_hi;
		unsigned long irq_pending_lo[IPIPE_IRQ_IWORDS];
		struct ipirqcnt {
			unsigned long pending_hits;
			unsigned long total_hits;
		} irq_counters[IPIPE_NR_IRQS];
		unsigned long long evsync;
	} ____cacheline_aligned_in_smp cpudata[IPIPE_NR_CPUS];

	struct {
		unsigned long control;
		ipipe_irq_ackfn_t acknowledge;
		ipipe_irq_handler_t handler;
		void *cookie;
	} ____cacheline_aligned irqs[IPIPE_NR_IRQS];

	ipipe_event_handler_t evhand[IPIPE_NR_EVENTS]; /* Event handlers. */
	unsigned long long evself;	/* Self-monitored event bits. */
	unsigned long flags;
	unsigned domid;
	const char *name;
	int priority;
	void *pdd;
	struct mutex mutex;
};

#define IPIPE_HEAD_PRIORITY	(-1) /* For domains always heading the pipeline */

struct ipipe_domain_attr {

	unsigned domid;		/* Domain identifier -- Magic value set by caller */
	const char *name;	/* Domain name -- Warning: won't be dup'ed! */
	int priority;		/* Priority in interrupt pipeline */
	void (*entry) (void);	/* Domain entry point */
	void *pdd;		/* Per-domain (opaque) data pointer */
};

/* The following macros must be used hw interrupts off. */

#define __ipipe_irq_cookie(ipd,irq)	(ipd)->irqs[irq].cookie
#define __ipipe_irq_handler(ipd,irq)	(ipd)->irqs[irq].handler

#define __ipipe_cpudata_irq_hits(ipd,cpuid,irq)	((ipd)->cpudata[cpuid].irq_counters[irq].total_hits)

#define __ipipe_set_irq_bit(ipd,cpuid,irq) \
do { \
	if (!test_bit(IPIPE_LOCK_FLAG,&(ipd)->irqs[irq].control)) { \
		__set_bit(irq & IPIPE_IRQ_IMASK,&(ipd)->cpudata[cpuid].irq_pending_lo[irq >> IPIPE_IRQ_ISHIFT]); \
		__set_bit(irq >> IPIPE_IRQ_ISHIFT,&(ipd)->cpudata[cpuid].irq_pending_hi); \
	} \
} while(0)

#define __ipipe_clear_pend(ipd,cpuid,irq) \
do { \
	__clear_bit(irq & IPIPE_IRQ_IMASK,&(ipd)->cpudata[cpuid].irq_pending_lo[irq >> IPIPE_IRQ_ISHIFT]); \
	if ((ipd)->cpudata[cpuid].irq_pending_lo[irq >> IPIPE_IRQ_ISHIFT] == 0) \
		__clear_bit(irq >> IPIPE_IRQ_ISHIFT,&(ipd)->cpudata[cpuid].irq_pending_hi); \
} while(0)

#define __ipipe_lock_irq(ipd,cpuid,irq) \
do { \
	if (!test_and_set_bit(IPIPE_LOCK_FLAG,&(ipd)->irqs[irq].control)) \
		__ipipe_clear_pend(ipd,cpuid,irq); \
} while(0)

#define __ipipe_unlock_irq(ipd,irq) \
do { \
	int __cpuid, __nr_cpus = num_online_cpus(); \
	if (test_and_clear_bit(IPIPE_LOCK_FLAG,&(ipd)->irqs[irq].control)) \
		for (__cpuid = 0; __cpuid < __nr_cpus; __cpuid++) \
			if ((ipd)->cpudata[__cpuid].irq_counters[irq].pending_hits > 0) { /* We need atomic ops next. */ \
				set_bit(irq & IPIPE_IRQ_IMASK,&(ipd)->cpudata[__cpuid].irq_pending_lo[irq >> IPIPE_IRQ_ISHIFT]); \
				set_bit(irq >> IPIPE_IRQ_ISHIFT,&(ipd)->cpudata[__cpuid].irq_pending_hi); \
			} \
} while(0)

#define __ipipe_clear_irq(ipd,irq) \
do { \
	int __cpuid, __nr_cpus = num_online_cpus(); \
	clear_bit(IPIPE_LOCK_FLAG,&(ipd)->irqs[irq].control); \
	for (__cpuid = 0; __cpuid < __nr_cpus; __cpuid++) { \
		(ipd)->cpudata[__cpuid].irq_counters[irq].pending_hits = 0; \
		__ipipe_clear_pend(ipd,__cpuid,irq); \
	} \
} while(0)

#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
#define write_lock_hw(x)		__raw_write_lock(&(x)->raw_lock)
#define write_trylock_hw(x)		__raw_write_trylock(&(x)->raw_lock)
#define write_unlock_hw(x)		__raw_write_unlock(&(x)->raw_lock)
#define read_lock_hw(x)		__raw_read_lock(&(x)->raw_lock)
#define read_trylock_hw(x)		__raw_read_trylock(&(x)->raw_lock)
#define read_unlock_hw(x)		__raw_read_unlock(&(x)->raw_lock)
#else /* UP non-debug */
#define write_lock_hw(lock)		do { (void)(lock); } while (0)
#define write_trylock_hw(lock)	({ (void)(lock); 1; })
#define write_unlock_hw(lock)		do { (void)(lock); } while (0)
#define read_lock_hw(lock)		do { (void)(lock); } while (0)
#define read_trylock_hw(lock)		({ (void)(lock); 1; })
#define read_unlock_hw(lock)		do { (void)(lock); } while (0)
#endif /* CONFIG_SMP || CONFIG_DEBUG_SPINLOCK */

typedef rwlock_t			ipipe_rwlock_t;
#define IPIPE_RW_LOCK_UNLOCKED		RW_LOCK_UNLOCKED

#define read_lock_irqsave_hw(lock, flags)	\
do {						\
	local_irq_save_hw(flags);		\
	read_lock_hw(lock);			\
} while (0)

#define read_unlock_irqrestore_hw(lock, flags)	\
do {						\
	read_unlock_hw(lock);			\
	local_irq_restore_hw(flags);		\
} while (0)

#define write_lock_irqsave_hw(lock, flags)	\
do {						\
	local_irq_save_hw(flags);		\
	write_lock_hw(lock);			\
} while (0)

#define write_unlock_irqrestore_hw(lock, flags)	\
do {						\
	write_unlock_hw(lock);			\
	local_irq_restore_hw(flags);		\
} while (0)

DECLARE_PER_CPU(struct ipipe_domain *, ipipe_percpu_domain);

extern struct ipipe_domain ipipe_root;

#define ipipe_root_domain (&ipipe_root)

extern unsigned __ipipe_printk_virq;

extern unsigned long __ipipe_virtual_irq_map;

extern struct list_head __ipipe_pipeline;

extern int __ipipe_event_monitors[];

/* Private interface */

void ipipe_init(void);

#ifdef CONFIG_PROC_FS
void ipipe_init_proc(void);

#ifdef CONFIG_IPIPE_TRACE
void __ipipe_init_tracer(void);
#else /* !CONFIG_IPIPE_TRACE */
#define __ipipe_init_tracer()       do { } while(0)
#endif /* CONFIG_IPIPE_TRACE */

#else	/* !CONFIG_PROC_FS */
#define ipipe_init_proc()	do { } while(0)
#endif	/* CONFIG_PROC_FS */

void __ipipe_init_stage(struct ipipe_domain *ipd);

void __ipipe_cleanup_domain(struct ipipe_domain *ipd);

void __ipipe_add_domain_proc(struct ipipe_domain *ipd);

void __ipipe_remove_domain_proc(struct ipipe_domain *ipd);

void __ipipe_flush_printk(unsigned irq, void *cookie);

void __ipipe_stall_root(void);

void __ipipe_unstall_root(void);

unsigned long __ipipe_test_root(void);

unsigned long __ipipe_test_and_stall_root(void);

void fastcall __ipipe_walk_pipeline(struct list_head *pos, int cpuid);

void fastcall __ipipe_restore_root(unsigned long x);

int fastcall __ipipe_schedule_irq(unsigned irq, struct list_head *head);

int fastcall __ipipe_dispatch_event(unsigned event, void *data);

int fastcall __ipipe_dispatch_wired(struct ipipe_domain *head, unsigned irq);

void fastcall __ipipe_sync_stage(unsigned long syncmask);

#ifndef __ipipe_sync_pipeline
#define __ipipe_sync_pipeline(syncmask) __ipipe_sync_stage(syncmask)
#endif

#ifndef __ipipe_run_irqtail
#define __ipipe_run_irqtail() do { } while(0)
#endif

#define __ipipe_pipeline_head_p(ipd) (&(ipd)->p_link == __ipipe_pipeline.next)

/*
 * Keep the following as a macro, so that client code could check for
 * the support of the invariant pipeline head optimization.
 */
#define __ipipe_pipeline_head() list_entry(__ipipe_pipeline.next,struct ipipe_domain,p_link)

#define __ipipe_event_monitored_p(ev) \
	(__ipipe_event_monitors[ev] > 0 || (ipipe_current_domain->evself & (1LL << ev)))

#ifdef CONFIG_SMP

cpumask_t __ipipe_set_irq_affinity(unsigned irq,
				   cpumask_t cpumask);

int fastcall __ipipe_send_ipi(unsigned ipi,
			      cpumask_t cpumask);

#endif /* CONFIG_SMP */

/* Called with hw interrupts off. */
static inline void __ipipe_switch_to(struct ipipe_domain *out,
				     struct ipipe_domain *in, int cpuid)
{
	void ipipe_suspend_domain(void);

	/*
	 * "in" is guaranteed to be closer than "out" from the head of the
	 * pipeline (and obviously different).
	 */

	out->cpudata[cpuid].evsync = 0;
	per_cpu(ipipe_percpu_domain, cpuid) = in;

	ipipe_suspend_domain();	/* Sync stage and propagate interrupts. */
	ipipe_load_cpuid();	/* Processor might have changed. */

	if (per_cpu(ipipe_percpu_domain, cpuid) == in)
		/*
		 * Otherwise, something has changed the current domain under
		 * our feet recycling the register set; do not override.
		 */
		per_cpu(ipipe_percpu_domain, cpuid) = out;
}

#define ipipe_sigwake_notify(p)	\
do {					\
	if (((p)->flags & PF_EVNOTIFY) && __ipipe_event_monitored_p(IPIPE_EVENT_SIGWAKE)) \
		__ipipe_dispatch_event(IPIPE_EVENT_SIGWAKE,p);		\
} while(0)

#define ipipe_exit_notify(p)	\
do {				\
	if (((p)->flags & PF_EVNOTIFY) && __ipipe_event_monitored_p(IPIPE_EVENT_EXIT)) \
		__ipipe_dispatch_event(IPIPE_EVENT_EXIT,p);		\
} while(0)

#define ipipe_setsched_notify(p)	\
do {					\
	if (((p)->flags & PF_EVNOTIFY) && __ipipe_event_monitored_p(IPIPE_EVENT_SETSCHED)) \
		__ipipe_dispatch_event(IPIPE_EVENT_SETSCHED,p);		\
} while(0)

#define ipipe_schedule_notify(prev, next)				\
do {									\
	if ((((prev)->flags|(next)->flags) & PF_EVNOTIFY) &&		\
	    __ipipe_event_monitored_p(IPIPE_EVENT_SCHEDULE))		\
		__ipipe_dispatch_event(IPIPE_EVENT_SCHEDULE,next);	\
} while(0)

#define ipipe_trap_notify(ex, regs)		\
({						\
	ipipe_declare_cpuid;			\
	int ret = 0;				\
	ipipe_load_cpuid();			\
	if ((test_bit(IPIPE_NOSTACK_FLAG, &ipipe_current_domain->cpudata[cpuid].status) || \
	     ((current)->flags & PF_EVNOTIFY)) &&			\
	    __ipipe_event_monitored_p(ex))				\
		ret = __ipipe_dispatch_event(ex, regs);			\
	ret;								\
})

static inline void ipipe_init_notify(struct task_struct *p)
{
	if (__ipipe_event_monitored_p(IPIPE_EVENT_INIT))
		__ipipe_dispatch_event(IPIPE_EVENT_INIT,p);
}

static inline void ipipe_cleanup_notify(struct mm_struct *mm)
{
	if (__ipipe_event_monitored_p(IPIPE_EVENT_CLEANUP))
		__ipipe_dispatch_event(IPIPE_EVENT_CLEANUP,mm);
}

/* Public interface */

int ipipe_register_domain(struct ipipe_domain *ipd,
			  struct ipipe_domain_attr *attr);

int ipipe_unregister_domain(struct ipipe_domain *ipd);

void ipipe_suspend_domain(void);

int ipipe_virtualize_irq(struct ipipe_domain *ipd,
			 unsigned irq,
			 ipipe_irq_handler_t handler,
			 void *cookie,
			 ipipe_irq_ackfn_t acknowledge,
			 unsigned modemask);

int ipipe_control_irq(unsigned irq,
		      unsigned clrmask,
		      unsigned setmask);

unsigned ipipe_alloc_virq(void);

int ipipe_free_virq(unsigned virq);

int fastcall ipipe_trigger_irq(unsigned irq);

static inline int ipipe_propagate_irq(unsigned irq)
{
	return __ipipe_schedule_irq(irq, ipipe_current_domain->p_link.next);
}

static inline int ipipe_schedule_irq(unsigned irq)
{
	return __ipipe_schedule_irq(irq, &ipipe_current_domain->p_link);
}

void fastcall ipipe_stall_pipeline_from(struct ipipe_domain *ipd);

unsigned long fastcall ipipe_test_and_stall_pipeline_from(struct ipipe_domain *ipd);

void fastcall ipipe_unstall_pipeline_from(struct ipipe_domain *ipd);

unsigned long fastcall ipipe_test_and_unstall_pipeline_from(struct ipipe_domain *ipd);

void fastcall ipipe_restore_pipeline_from(struct ipipe_domain *ipd,
					  unsigned long x);

static inline unsigned long ipipe_test_pipeline_from(struct ipipe_domain *ipd)
{
	unsigned long flags, x;
	ipipe_declare_cpuid;

	ipipe_get_cpu(flags);
	x = test_bit(IPIPE_STALL_FLAG, &ipd->cpudata[cpuid].status);
	ipipe_put_cpu(flags);

	return x;
}

static inline void ipipe_restore_pipeline_nosync(struct ipipe_domain *ipd,
						 unsigned long x, int cpuid)
{
	/*
	 * If cpuid is current, then it must be held on entry
	 * (ipipe_get_cpu/local_irq_save_hw/local_irq_disable_hw).
	 */

	if (x)
		__set_bit(IPIPE_STALL_FLAG, &ipd->cpudata[cpuid].status);
	else
		__clear_bit(IPIPE_STALL_FLAG, &ipd->cpudata[cpuid].status);
}

static inline void ipipe_stall_pipeline_head(void)
{
	ipipe_declare_cpuid;

	local_irq_disable_hw();
	ipipe_load_cpuid();
	__set_bit(IPIPE_STALL_FLAG, &__ipipe_pipeline_head()->cpudata[cpuid].status);
}

static inline unsigned long ipipe_test_and_stall_pipeline_head(void)
{
	ipipe_declare_cpuid;

	local_irq_disable_hw();
	ipipe_load_cpuid();
	return __test_and_set_bit(IPIPE_STALL_FLAG, &__ipipe_pipeline_head()->cpudata[cpuid].status);
}

void ipipe_unstall_pipeline_head(void);

void fastcall __ipipe_restore_pipeline_head(struct ipipe_domain *head,
					    unsigned long x);

static inline void ipipe_restore_pipeline_head(unsigned long x)
{
	struct ipipe_domain *head = __ipipe_pipeline_head();
	/* On some archs, __test_and_set_bit() might return different
	 * truth value than test_bit(), so we test the exclusive OR of
	 * both statuses, assuming that the lowest bit is always set in
	 * the truth value (if this is wrong, the failed optimization will
	 * be caught in __ipipe_restore_pipeline_head() if
	 * CONFIG_DEBUG_KERNEL is set). */
	if ((x ^ test_bit(IPIPE_STALL_FLAG, &head->cpudata[ipipe_processor_id()].status)) & 1)
		__ipipe_restore_pipeline_head(head,x);
}

#define ipipe_unstall_pipeline() \
	ipipe_unstall_pipeline_from(ipipe_current_domain)

#define ipipe_test_and_unstall_pipeline() \
	ipipe_test_and_unstall_pipeline_from(ipipe_current_domain)

#define ipipe_test_pipeline() \
	ipipe_test_pipeline_from(ipipe_current_domain)

#define ipipe_test_and_stall_pipeline() \
	ipipe_test_and_stall_pipeline_from(ipipe_current_domain)

#define ipipe_stall_pipeline() \
	ipipe_stall_pipeline_from(ipipe_current_domain)

#define ipipe_restore_pipeline(x) \
	ipipe_restore_pipeline_from(ipipe_current_domain, (x))

void ipipe_init_attr(struct ipipe_domain_attr *attr);

int ipipe_get_sysinfo(struct ipipe_sysinfo *sysinfo);

int ipipe_tune_timer(unsigned long ns,
		     int flags);

unsigned long ipipe_critical_enter(void (*syncfn) (void));

void ipipe_critical_exit(unsigned long flags);

static inline void ipipe_set_printk_sync(struct ipipe_domain *ipd)
{
	set_bit(IPIPE_SPRINTK_FLAG, &ipd->flags);
}

static inline void ipipe_set_printk_async(struct ipipe_domain *ipd)
{
	clear_bit(IPIPE_SPRINTK_FLAG, &ipd->flags);
}

static inline void ipipe_set_foreign_stack(struct ipipe_domain *ipd)
{
	/* Must be called hw interrupts off. */
	ipipe_declare_cpuid;
	ipipe_load_cpuid();
	__set_bit(IPIPE_NOSTACK_FLAG, &ipd->cpudata[cpuid].status);
}

static inline void ipipe_clear_foreign_stack(struct ipipe_domain *ipd)
{
	/* Must be called hw interrupts off. */
	ipipe_declare_cpuid;
	ipipe_load_cpuid();
	__clear_bit(IPIPE_NOSTACK_FLAG, &ipd->cpudata[cpuid].status);
}

#define ipipe_safe_current()					\
({								\
	ipipe_declare_cpuid;					\
	struct task_struct *p;					\
	ipipe_load_cpuid();					\
	p = test_bit(IPIPE_NOSTACK_FLAG,			\
		     &per_cpu(ipipe_percpu_domain, cpuid)->cpudata[cpuid].status) ? &init_task : current; \
	p; \
})

ipipe_event_handler_t ipipe_catch_event(struct ipipe_domain *ipd,
					unsigned event,
					ipipe_event_handler_t handler);

cpumask_t ipipe_set_irq_affinity(unsigned irq,
				 cpumask_t cpumask);

int fastcall ipipe_send_ipi(unsigned ipi,
			    cpumask_t cpumask);

int ipipe_setscheduler_root(struct task_struct *p,
			    int policy,
			    int prio);

int ipipe_reenter_root(struct task_struct *prev,
		       int policy,
		       int prio);

int ipipe_alloc_ptdkey(void);

int ipipe_free_ptdkey(int key);

int fastcall ipipe_set_ptd(int key,
			   void *value);

void fastcall *ipipe_get_ptd(int key);

#define local_irq_enable_hw_cond()		local_irq_enable_hw()
#define local_irq_disable_hw_cond()		local_irq_disable_hw()
#define local_irq_save_hw_cond(flags)	local_irq_save_hw(flags)
#define local_irq_restore_hw_cond(flags)	local_irq_restore_hw(flags)
#define local_irq_disable_head()		ipipe_stall_pipeline_head()

#define local_irq_enable_nohead(ipd)			\
	do {						\
		if (!__ipipe_pipeline_head_p(ipd))	\
			local_irq_enable_hw();		\
	} while(0)

#define local_irq_disable_nohead(ipd)		\
	do {						\
		if (!__ipipe_pipeline_head_p(ipd))	\
			local_irq_disable_hw();		\
	} while(0)

#define smp_processor_id_hw()				ipipe_processor_id()

#define ipipe_irq_lock(irq)						\
	do {								\
		ipipe_declare_cpuid;					\
		ipipe_load_cpuid();					\
		__ipipe_lock_irq(per_cpu(ipipe_percpu_domain, cpuid), cpuid, irq);\
	} while(0)

#define ipipe_irq_unlock(irq)						\
	do {								\
		ipipe_declare_cpuid;					\
		ipipe_load_cpuid();					\
		__ipipe_unlock_irq(per_cpu(ipipe_percpu_domain, cpuid), irq);	\
	} while(0)

#define ipipe_root_domain_p		(ipipe_current_domain == ipipe_root_domain)

#else	/* !CONFIG_IPIPE */

#define ipipe_init()			do { } while(0)
#define ipipe_suspend_domain()	do { } while(0)
#define ipipe_sigwake_notify(p)	do { } while(0)
#define ipipe_setsched_notify(p)	do { } while(0)
#define ipipe_init_notify(p)		do { } while(0)
#define ipipe_exit_notify(p)		do { } while(0)
#define ipipe_cleanup_notify(mm)	do { } while(0)
#define ipipe_trap_notify(t,r)	0
#define ipipe_init_proc()		do { } while(0)

#define local_irq_enable_hw_cond()		do { } while(0)
#define local_irq_disable_hw_cond()		do { } while(0)
#define local_irq_save_hw_cond(flags)	do { (void)(flags); } while(0)
#define local_irq_restore_hw_cond(flags)	do { } while(0)
#define smp_processor_id_hw()			smp_processor_id()

#define ipipe_irq_lock(irq)		do { } while(0)
#define ipipe_irq_unlock(irq)		do { } while(0)

#define ipipe_root_domain_p		1
#define ipipe_safe_current		current

#define local_irq_disable_head()	local_irq_disable()

#endif	/* CONFIG_IPIPE */

#endif	/* !__LINUX_IPIPE_H */
