/* -*- linux-c -*-
 * linux/arch/powerpc/kernel/ipipe.c
 *
 * Copyright (C) 2002-2007 Philippe Gerum.
 * Copyright (C) 2004 Wolfgang Grandegger (Adeos/ppc port over 2.4).
 * Copyright (C) 2005 Heikki Lindholm (PowerPC 970 fixes).
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
 *
 * Architecture-dependent I-PIPE core support for PowerPC.
 */

#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <asm/system.h>
#include <asm/machdep.h>
#include <asm/atomic.h>
#include <asm/hardirq.h>
#include <asm/io.h>
#include <asm/time.h>

/* Current reload value for the decrementer. */
unsigned long __ipipe_decr_ticks;

/* Next tick date (timebase value). */
unsigned long long __ipipe_decr_next[IPIPE_NR_CPUS];

struct pt_regs __ipipe_tick_regs[IPIPE_NR_CPUS];

static void __ipipe_do_IRQ(unsigned irq, void *cookie);

static void __ipipe_do_timer(unsigned irq, void *cookie);

#ifdef CONFIG_SMP

static cpumask_t __ipipe_cpu_sync_map;

static cpumask_t __ipipe_cpu_lock_map;

static ipipe_spinlock_t __ipipe_cpu_barrier = IPIPE_SPIN_LOCK_UNLOCKED;

static atomic_t __ipipe_critical_count = ATOMIC_INIT(0);

static void (*__ipipe_cpu_sync) (void);

void __ipipe_hook_critical_ipi(struct ipipe_domain *ipd)
{
	BUG();	/* SMP not fully implemented yet. */
}

/* Always called with hw interrupts off. */

void __ipipe_do_critical_sync(unsigned irq)
{
	ipipe_declare_cpuid;

	ipipe_load_cpuid();

	cpu_set(cpuid, __ipipe_cpu_sync_map);

	/*
	 * Now we are in sync with the lock requestor running on another
	 * CPU. Enter a spinning wait until he releases the global
	 * lock.
	 */
	spin_lock_hw(&__ipipe_cpu_barrier);

	/* Got it. Now get out. */

	if (__ipipe_cpu_sync)
		/* Call the sync routine if any. */
		__ipipe_cpu_sync();

	spin_unlock_hw(&__ipipe_cpu_barrier);

	cpu_clear(cpuid, __ipipe_cpu_sync_map);
}

#endif	/* CONFIG_SMP */

/*
 * ipipe_critical_enter() -- Grab the superlock excluding all CPUs
 * but the current one from a critical section. This lock is used when
 * we must enforce a global critical section for a single CPU in a
 * possibly SMP system whichever context the CPUs are running.
 */
unsigned long ipipe_critical_enter(void (*syncfn) (void))
{
	unsigned long flags;

	local_irq_save_hw(flags);

#ifdef CONFIG_SMP
	if (num_online_cpus() > 1) {	/* We might be running a SMP-kernel on a UP box... */
		ipipe_declare_cpuid;
		cpumask_t lock_map;

		ipipe_load_cpuid();

		if (!cpu_test_and_set(cpuid, __ipipe_cpu_lock_map)) {
			while (cpu_test_and_set(BITS_PER_LONG - 1,
						__ipipe_cpu_lock_map)) {
				int n = 0;
				do {
					cpu_relax();
				} while (++n < cpuid);
			}

			spin_lock_hw(&__ipipe_cpu_barrier);

			__ipipe_cpu_sync = syncfn;

			/* Send the sync IPI to all processors but the current one. */
			send_IPI_allbutself(IPIPE_CRITICAL_VECTOR);

			cpus_andnot(lock_map, cpu_online_map,
				    __ipipe_cpu_lock_map);

			while (!cpus_equal(__ipipe_cpu_sync_map, lock_map))
				cpu_relax();
		}

		atomic_inc(&__ipipe_critical_count);
	}
#endif	/* CONFIG_SMP */

	return flags;
}

/* ipipe_critical_exit() -- Release the superlock. */

void ipipe_critical_exit(unsigned long flags)
{
#ifdef CONFIG_SMP
	if (num_online_cpus() > 1) {	/* We might be running a SMP-kernel on a UP box... */
		ipipe_declare_cpuid;

		ipipe_load_cpuid();

		if (atomic_dec_and_test(&__ipipe_critical_count)) {
			spin_unlock_hw(&__ipipe_cpu_barrier);

			while (!cpus_empty(__ipipe_cpu_sync_map))
				cpu_relax();

			cpu_clear(cpuid, __ipipe_cpu_lock_map);
			cpu_clear(BITS_PER_LONG - 1, __ipipe_cpu_lock_map);
		}
	}
#endif	/* CONFIG_SMP */

	local_irq_restore_hw(flags);
}

void __ipipe_init_platform(void)
{
	unsigned timer_virq;

	/*
	 * Allocate a virtual IRQ for the decrementer trap early to
	 * get it mapped to IPIPE_VIRQ_BASE
	 */

	timer_virq = ipipe_alloc_virq();

	if (timer_virq != IPIPE_TIMER_VIRQ)
		panic("I-pipe: cannot reserve timer virq #%d (got #%d)",
		      IPIPE_TIMER_VIRQ, timer_virq);

	__ipipe_decr_ticks = tb_ticks_per_jiffy;
}

int __ipipe_ack_irq(unsigned irq)
{
	irq_desc_t *desc = irq_desc + irq;
	desc->ipipe_ack(irq, desc);
	return 1;
}

void __ipipe_enable_irqdesc(unsigned irq)
{
	irq_desc[irq].status &= ~IRQ_DISABLED;
}

static void __ipipe_enable_sync(void)
{
	__ipipe_decr_next[ipipe_processor_id()] =
		__ipipe_read_timebase() + get_dec();
}

/*
 * __ipipe_enable_pipeline() -- We are running on the boot CPU, hw
 * interrupts are off, and secondary CPUs are still lost in space.
 */
void __ipipe_enable_pipeline(void)
{
	unsigned long flags;
	unsigned irq;

	flags = ipipe_critical_enter(&__ipipe_enable_sync);

	/* First, virtualize all interrupts from the root domain. */

	for (irq = 0; irq < NR_IRQS; irq++)
		ipipe_virtualize_irq(ipipe_root_domain,
				     irq,
				     &__ipipe_do_IRQ, NULL,
				     &__ipipe_ack_irq,
				     IPIPE_HANDLE_MASK | IPIPE_PASS_MASK);

	/*
	 * We use a virtual IRQ to handle the timer irq (decrementer trap)
	 * which has been allocated early in __ipipe_init_platform().
	 */

	ipipe_virtualize_irq(ipipe_root_domain,
			     IPIPE_TIMER_VIRQ,
			     &__ipipe_do_timer, NULL,
			     NULL, IPIPE_HANDLE_MASK | IPIPE_PASS_MASK);


	__ipipe_decr_next[ipipe_processor_id()] =
		__ipipe_read_timebase() + get_dec();

	ipipe_critical_exit(flags);
}

int ipipe_get_sysinfo(struct ipipe_sysinfo *info)
{
	info->ncpus = num_online_cpus();
	info->cpufreq = ipipe_cpu_freq();
	info->archdep.tmirq = IPIPE_TIMER_VIRQ;
	info->archdep.tmfreq = info->cpufreq;

	return 0;
}

/*
 * ipipe_trigger_irq() -- Push the interrupt at front of the pipeline
 * just like if it has been actually received from a hw source. Also
 * works for virtual interrupts.
 */
int ipipe_trigger_irq(unsigned irq)
{
	unsigned long flags;

	if (irq >= IPIPE_NR_IRQS ||
	    (ipipe_virtual_irq_p(irq)
	     && !test_bit(irq - IPIPE_VIRQ_BASE, &__ipipe_virtual_irq_map)))
		return -EINVAL;

	local_irq_save_hw(flags);

	__ipipe_handle_irq(irq, NULL);

	local_irq_restore_hw(flags);

	return 1;
}

static void __ipipe_set_decr(void)
{
	ipipe_declare_cpuid;

	ipipe_load_cpuid();

	disarm_decr[cpuid] = (__ipipe_decr_ticks != tb_ticks_per_jiffy);
#ifdef CONFIG_40x
	/* Enable and set auto-reload. */
	mtspr(SPRN_TCR, mfspr(SPRN_TCR) | TCR_ARE);
	mtspr(SPRN_PIT, __ipipe_decr_ticks);
#else	/* !CONFIG_40x */
	__ipipe_decr_next[cpuid] = __ipipe_read_timebase() + __ipipe_decr_ticks;
	set_dec(__ipipe_decr_ticks);
#endif	/* CONFIG_40x */
}

int ipipe_tune_timer(unsigned long ns, int flags)
{
	unsigned long x, ticks;

	if (flags & IPIPE_RESET_TIMER)
		ticks = tb_ticks_per_jiffy;
	else {
		ticks = (ns / 1000) * tb_ticks_per_jiffy / (1000000 / HZ);

		if (ticks > tb_ticks_per_jiffy)
			return -EINVAL;
	}

	x = ipipe_critical_enter(&__ipipe_set_decr);	/* Sync with all CPUs */
	__ipipe_decr_ticks = ticks;
	__ipipe_set_decr();
	ipipe_critical_exit(x);

	return 0;
}

/*
 * __ipipe_handle_irq() -- IPIPE's generic IRQ handler. An optimistic
 * interrupt protection log is maintained here for each domain. Hw
 * interrupts are off on entry.
 */
void __ipipe_handle_irq(int irq, struct pt_regs *regs)
{
	struct ipipe_domain *this_domain, *next_domain;
	struct list_head *head, *pos;
	ipipe_declare_cpuid;
	int m_ack;

	m_ack = (regs == NULL);	/* Software-triggered IRQs do not need
				 * any ack. */
	if (irq >= IPIPE_NR_IRQS) {
		printk(KERN_ERR "I-pipe: spurious interrupt %d\n", irq);
		return;
	}

	ipipe_load_cpuid();

	this_domain = per_cpu(ipipe_percpu_domain, cpuid);

	if (unlikely(test_bit(IPIPE_STICKY_FLAG, &this_domain->irqs[irq].control)))
		head = &this_domain->p_link;
	else {
		head = __ipipe_pipeline.next;
		next_domain = list_entry(head, struct ipipe_domain, p_link);
		if (likely(test_bit(IPIPE_WIRED_FLAG, &next_domain->irqs[irq].control))) {
			if (!m_ack && next_domain->irqs[irq].acknowledge != NULL)
				next_domain->irqs[irq].acknowledge(irq);
			if (likely(__ipipe_dispatch_wired(next_domain, irq)))
			    goto finalize;
			return;
		}
	}

	/* Ack the interrupt. */

	pos = head;

	while (pos != &__ipipe_pipeline) {
		next_domain = list_entry(pos, struct ipipe_domain, p_link);
		/*
		 * For each domain handling the incoming IRQ, mark it as
		 * pending in its log.
		 */
		if (test_bit(IPIPE_HANDLE_FLAG,
			     &next_domain->irqs[irq].control)) {
			/*
			 * Domains that handle this IRQ are polled for
			 * acknowledging it by decreasing priority order. The
			 * interrupt must be made pending _first_ in the
			 * domain's status flags before the PIC is unlocked.
			 */

			next_domain->cpudata[cpuid].irq_counters[irq].total_hits++;
			next_domain->cpudata[cpuid].irq_counters[irq].pending_hits++;
			__ipipe_set_irq_bit(next_domain, cpuid, irq);

			/*
			 * Always get the first master acknowledge available.
			 * Once we've got it, allow slave acknowledge
			 * handlers to run (until one of them stops us).
			 */
			if (next_domain->irqs[irq].acknowledge != NULL && !m_ack)
				m_ack = next_domain->irqs[irq].acknowledge(irq);
		}

		/*
		 * If the domain does not want the IRQ to be passed down the
		 * interrupt pipe, exit the loop now.
		 */

		if (!test_bit(IPIPE_PASS_FLAG, &next_domain->irqs[irq].control))
			break;

		pos = next_domain->p_link.next;
	}

finalize:
	/*
	 * Now walk the pipeline, yielding control to the highest
	 * priority domain that has pending interrupt(s) or
	 * immediately to the current domain if the interrupt has been
	 * marked as 'sticky'. This search does not go beyond the
	 * current domain in the pipeline.
	 */

	__ipipe_walk_pipeline(head, cpuid);
}

int __ipipe_grab_irq(struct pt_regs *regs)
{
	extern int ppc_spurious_interrupts;
	ipipe_declare_cpuid;
	int irq;

	irq = ppc_md.get_irq();

	if (irq != NO_IRQ && irq != NO_IRQ_IGNORE) {
#ifdef CONFIG_IPIPE_TRACE_IRQSOFF
		ipipe_trace_begin(irq);
#endif /* CONFIG_IPIPE_TRACE_IRQSOFF */
		__ipipe_handle_irq(irq, regs);
#ifdef CONFIG_IPIPE_TRACE_IRQSOFF
		ipipe_trace_end(irq);
#endif /* CONFIG_IPIPE_TRACE_IRQSOFF */
	} else if (irq != NO_IRQ_IGNORE)
		ppc_spurious_interrupts++;

	ipipe_load_cpuid();

	return (per_cpu(ipipe_percpu_domain, cpuid) == ipipe_root_domain &&
		!test_bit(IPIPE_STALL_FLAG,
			  &ipipe_root_domain->cpudata[cpuid].status));
}

static void __ipipe_do_IRQ(unsigned irq, void *cookie)
{
	struct pt_regs *old_regs;
#ifdef CONFIG_IRQSTACKS
	struct thread_info *curtp, *irqtp;
#endif

	/* Provide a valid register frame, even if not the exact one. */
	old_regs = set_irq_regs(__ipipe_tick_regs + smp_processor_id());

	irq_enter();

#ifdef CONFIG_DEBUG_STACKOVERFLOW
	/* Debugging check for stack overflow: is there less than 2KB free? */
	{
		long sp;

		sp = __get_SP() & (THREAD_SIZE-1);

		if (unlikely(sp < (sizeof(struct thread_info) + 2048))) {
			printk("do_IRQ: stack overflow: %ld\n",
				sp - sizeof(struct thread_info));
			dump_stack();
		}
	}
#endif

#ifdef CONFIG_IRQSTACKS
		/* Switch to the irq stack to handle this */
		curtp = current_thread_info();
		irqtp = hardirq_ctx[smp_processor_id()];
		if (curtp != irqtp) {
			struct irq_desc *desc = irq_desc + irq;
			void *handler = desc->handle_irq;
			if (handler == NULL)
				handler = &__do_IRQ;
			irqtp->task = curtp->task;
			irqtp->flags = 0;
			call_handle_irq(irq, desc, irqtp, handler);
			irqtp->task = NULL;
			if (irqtp->flags)
				set_bits(irqtp->flags, &curtp->flags);
		} else
#endif
			generic_handle_irq(irq);

	irq_exit();

	set_irq_regs(old_regs);
}

static void __ipipe_do_timer(unsigned irq, void *cookie)
{
	timer_interrupt(__ipipe_tick_regs + smp_processor_id());
}

int __ipipe_grab_timer(struct pt_regs *regs)
{
	ipipe_declare_cpuid;

#ifdef CONFIG_IPIPE_TRACE_IRQSOFF
	ipipe_trace_begin(IPIPE_TIMER_VIRQ);
#endif /* CONFIG_IPIPE_TRACE_IRQSOFF */

#ifdef CONFIG_POWER4
	/* On 970 CPUs DEC cannot be disabled, and without setting DEC
	 * here, DEC interrupt would be triggered as soon as interrupts
	 * are enabled in __ipipe_sync_stage
	 */
	set_dec(0x7fffffff);
#endif

	__ipipe_tick_regs[cpuid].msr = regs->msr; /* for timer_interrupt() */

#ifndef CONFIG_40x
	if (__ipipe_decr_ticks != tb_ticks_per_jiffy) {
		unsigned long long next_date, now;

		next_date = __ipipe_decr_next[cpuid];

		while ((now = __ipipe_read_timebase()) >= next_date)
			next_date += __ipipe_decr_ticks;

		set_dec(next_date - now);

		__ipipe_decr_next[cpuid] = next_date;
	}
#endif	/* !CONFIG_40x */

	__ipipe_handle_irq(IPIPE_TIMER_VIRQ, NULL);

#ifdef CONFIG_IPIPE_TRACE_IRQSOFF
	ipipe_trace_end(IPIPE_TIMER_VIRQ);
#endif /* CONFIG_IPIPE_TRACE_IRQSOFF */

	ipipe_load_cpuid();

	return (per_cpu(ipipe_percpu_domain, cpuid) == ipipe_root_domain &&
		!test_bit(IPIPE_STALL_FLAG,
			  &ipipe_root_domain->cpudata[cpuid].status));
}

int __ipipe_check_root(struct pt_regs *regs)
{
	ipipe_declare_cpuid;
	/*
	 * This routine is called with hw interrupts off, so no migration
	 * can occur while checking the identity of the current domain.
	 */
	ipipe_load_cpuid();
	return per_cpu(ipipe_percpu_domain, cpuid) == ipipe_root_domain;
}

void __ipipe_fast_stall_root(void)
{
	ipipe_declare_cpuid;
	unsigned long flags;

	ipipe_get_cpu(flags); /* Care for migration. */

	set_bit(IPIPE_STALL_FLAG,
		&ipipe_root_domain->cpudata[cpuid].status);

	ipipe_put_cpu(flags);
}

void __ipipe_fast_unstall_root(void)
{
	ipipe_declare_cpuid;
	unsigned long flags;

	ipipe_get_cpu(flags); /* Care for migration. */

	clear_bit(IPIPE_STALL_FLAG,
		  &ipipe_root_domain->cpudata[cpuid].status);

	ipipe_put_cpu(flags);
}

int __ipipe_syscall_root(struct pt_regs *regs)
{
	ipipe_declare_cpuid;
	unsigned long flags;

	/*
	 * This routine either returns:
	 * 0 -- if the syscall is to be passed to Linux;
	 * >0 -- if the syscall should not be passed to Linux, and no
	 * tail work should be performed;
	 * <0 -- if the syscall should not be passed to Linux but the
	 * tail work has to be performed (for handling signals etc).
	 */

	if (__ipipe_syscall_watched_p(current, regs->gpr[0]) &&
	    __ipipe_event_monitored_p(IPIPE_EVENT_SYSCALL) &&
	    __ipipe_dispatch_event(IPIPE_EVENT_SYSCALL,regs) > 0) {
		if (ipipe_current_domain == ipipe_root_domain && !in_atomic()) {
			/*
			 * Sync pending VIRQs before _TIF_NEED_RESCHED
			 * is tested.
			 */
			ipipe_lock_cpu(flags);
			if ((ipipe_root_domain->cpudata[cpuid].irq_pending_hi & IPIPE_IRQMASK_VIRT) != 0)
				__ipipe_sync_pipeline(IPIPE_IRQMASK_VIRT);
			ipipe_unlock_cpu(flags);
			return -1;
		}
		return 1;
	}

	return 0;
}

int __ipipe_pin_range_mapping(struct mm_struct *mm,
			      unsigned long start, unsigned long end)
{
	/* Actually, we don't need this for this arch (yet?). */
	return 0;
}

EXPORT_SYMBOL(__ipipe_decr_ticks);
EXPORT_SYMBOL(__ipipe_decr_next);
EXPORT_SYMBOL(ipipe_critical_enter);
EXPORT_SYMBOL(ipipe_critical_exit);
EXPORT_SYMBOL(ipipe_trigger_irq);
EXPORT_SYMBOL(ipipe_get_sysinfo);
EXPORT_SYMBOL(ipipe_tune_timer);

void atomic_set_mask(unsigned long mask,
		     unsigned long *ptr);

void atomic_clear_mask(unsigned long mask,
		       unsigned long *ptr);

extern unsigned long context_map[];

EXPORT_SYMBOL(disarm_decr);
EXPORT_SYMBOL_GPL(__switch_to);
EXPORT_SYMBOL_GPL(show_stack);
EXPORT_SYMBOL_GPL(atomic_set_mask);
EXPORT_SYMBOL_GPL(atomic_clear_mask);
EXPORT_SYMBOL_GPL(context_map);
EXPORT_SYMBOL_GPL(_switch);
EXPORT_SYMBOL_GPL(last_task_used_math);
#ifdef FEW_CONTEXTS
EXPORT_SYMBOL_GPL(nr_free_contexts);
EXPORT_SYMBOL_GPL(context_mm);
EXPORT_SYMBOL_GPL(steal_context);
#endif

#ifdef CONFIG_IPIPE_TRACE_MCOUNT
void notrace _mcount(void);
EXPORT_SYMBOL(_mcount);
#endif /* CONFIG_IPIPE_TRACE_MCOUNT */
