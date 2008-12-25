/* -*- linux-c -*-
 * linux/arch/powerpc/kernel/ipipe.c
 *
 * Copyright (C) 2005 Heikki Lindholm (PPC64 port).
 * Copyright (C) 2004 Wolfgang Grandegger (Adeos/ppc port over 2.4).
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
 *
 * Architecture-dependent I-PIPE core support for PowerPC 32/64bit.
 */

#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/kernel_stat.h>
#include <asm/reg.h>
#include <asm/system.h>
#include <asm/mmu_context.h>
#include <asm/unistd.h>
#include <asm/machdep.h>
#include <asm/atomic.h>
#include <asm/hardirq.h>
#include <asm/io.h>
#include <asm/time.h>

static void __ipipe_do_IRQ(unsigned irq, void *cookie);

static void __ipipe_do_timer(unsigned irq, void *cookie);

DEFINE_PER_CPU(struct pt_regs, __ipipe_tick_regs);
#ifdef CONFIG_IPIPE_UNMASKED_CONTEXT_SWITCH
DEFINE_PER_CPU(struct mm_struct *, ipipe_active_mm);
EXPORT_PER_CPU_SYMBOL(ipipe_active_mm);
#endif

#define DECREMENTER_MAX	0x7fffffff

#ifdef CONFIG_SMP

static cpumask_t __ipipe_cpu_sync_map;

static cpumask_t __ipipe_cpu_lock_map;

static ipipe_spinlock_t __ipipe_cpu_barrier = IPIPE_SPIN_LOCK_UNLOCKED;

static atomic_t __ipipe_critical_count = ATOMIC_INIT(0);

static void (*__ipipe_cpu_sync) (void);

static DEFINE_PER_CPU(struct ipipe_ipi_struct, ipipe_ipi_message);

unsigned int __ipipe_ipi_irq = NR_IRQS + 1; /* dummy value */

#ifdef CONFIG_DEBUGGER
cpumask_t __ipipe_dbrk_pending;	/* pending debugger break IPIs */
#endif

/* Always called with hw interrupts off. */

void __ipipe_do_critical_sync(unsigned irq, void *cookie)
{
	cpu_set(ipipe_processor_id(), __ipipe_cpu_sync_map);

	/*
	 * Now we are in sync with the lock requestor running on another
	 * CPU. Enter a spinning wait until he releases the global
	 * lock.
	 */
	spin_lock(&__ipipe_cpu_barrier);

	/* Got it. Now get out. */

	if (__ipipe_cpu_sync)
		/* Call the sync routine if any. */
		__ipipe_cpu_sync();

	spin_unlock(&__ipipe_cpu_barrier);

	cpu_clear(ipipe_processor_id(), __ipipe_cpu_sync_map);
}

void __ipipe_hook_critical_ipi(struct ipipe_domain *ipd)
{
	ipd->irqs[IPIPE_CRITICAL_IPI].acknowledge = NULL;
	ipd->irqs[IPIPE_CRITICAL_IPI].handler = &__ipipe_do_critical_sync;
	ipd->irqs[IPIPE_CRITICAL_IPI].cookie = NULL;
	/* Immediately handle in the current domain but *never* pass */
	ipd->irqs[IPIPE_CRITICAL_IPI].control =
		IPIPE_HANDLE_MASK|IPIPE_STICKY_MASK|IPIPE_SYSTEM_MASK;
}

void __ipipe_register_ipi(unsigned int irq)
{
	__ipipe_ipi_irq = irq;
	mb();
} 

static void __ipipe_ipi_demux(int irq, struct pt_regs *regs)
{
	int ipi, cpu = ipipe_processor_id();
	irq_desc_t *desc = irq_desc + irq;

	desc->ipipe_ack(irq, desc);

	kstat_cpu(cpu).irqs[irq]++;

	while (per_cpu(ipipe_ipi_message, cpu).value & IPIPE_MSG_IPI_MASK) {
		for (ipi = IPIPE_MSG_CRITICAL_IPI; ipi <= IPIPE_MSG_SERVICE_IPI4; ++ipi) {
			if (test_and_clear_bit(ipi, &per_cpu(ipipe_ipi_message, cpu).value)) {
				mb();
				__ipipe_handle_irq(ipi + IPIPE_MSG_IPI_OFFSET, NULL);
			}
		}
	}

#ifdef CONFIG_DEBUGGER
	/*
	 * The debugger IPI handler should be NMI-safe, so let's call
	 * it immediately in case the IPI is pending.
	 */
	if (cpu_isset(cpu, __ipipe_dbrk_pending)) {
		cpu_clear(cpu, __ipipe_dbrk_pending);
		debugger_ipi(regs);
	}
#endif /* CONFIG_DEBUGGER */

	__ipipe_end_irq(irq);
}

cpumask_t __ipipe_set_irq_affinity(unsigned irq, cpumask_t cpumask)
{
	cpumask_t oldmask = irq_desc[irq].affinity;

	if (irq_desc[irq].chip->set_affinity == NULL)
		return CPU_MASK_NONE;

	if (cpus_empty(cpumask))
		return oldmask; /* Return mask value -- no change. */

	cpus_and(cpumask,cpumask,cpu_online_map);

	if (cpus_empty(cpumask))
		return CPU_MASK_NONE;	/* Error -- bad mask value or non-routable IRQ. */

	irq_desc[irq].chip->set_affinity(irq,cpumask);

	return oldmask;
}

int __ipipe_send_ipi(unsigned ipi, cpumask_t cpumask)
{
	extern void mpic_send_ipi(unsigned int ipi_no, unsigned int cpu_mask);
	unsigned long flags;
	cpumask_t testmask;
	int cpu;

	local_irq_save_hw(flags);

	ipi -= IPIPE_MSG_IPI_OFFSET;
	for_each_online_cpu(cpu) {
		if (cpu_isset(cpu, cpumask))
			set_bit(ipi, &per_cpu(ipipe_ipi_message, cpu).value);
	}
	mb();	 
	
	if (unlikely(cpus_empty(cpumask)))
		goto out;

	cpus_setall(testmask);
	cpu_clear(ipipe_processor_id(), testmask);
	if (likely(cpus_equal(cpumask, testmask)))
		smp_ops->message_pass(MSG_ALL_BUT_SELF, PPC_MSG_IPIPE_DEMUX);
	else {
		/* Long path. */
		for_each_cpu_mask_nr(cpu, cpumask)
			smp_ops->message_pass(cpu, PPC_MSG_IPIPE_DEMUX);
	}
out:
	local_irq_restore_hw(flags);

	return 0;
}

void __ipipe_stall_root(void)
{
	set_bit_safe(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status));
}

unsigned long __ipipe_test_and_stall_root(void)
{
	return test_and_set_bit_safe(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status));
}

unsigned long __ipipe_test_root(void)
{
	return test_bit(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status));
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
	if (likely(num_online_cpus() > 1)) {
		/* We might be running a SMP-kernel on a UP box... */
		int cpu = ipipe_processor_id();
		cpumask_t lock_map;
		cpumask_t others;

		if (!cpu_test_and_set(cpu, __ipipe_cpu_lock_map)) {
			while (cpu_test_and_set(BITS_PER_LONG - 1, __ipipe_cpu_lock_map)) {
				int n = 0;
				do {
					cpu_relax();
				} while (++n < cpu);
			}

			spin_lock(&__ipipe_cpu_barrier);

			__ipipe_cpu_sync = syncfn;

			/* Send the sync IPI to all processors but the current one. */
			cpus_setall(others);
			cpu_clear(ipipe_processor_id(), others);
			__ipipe_send_ipi(IPIPE_CRITICAL_IPI, others);

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
	if (likely(num_online_cpus() > 1)) {
		/* We might be running a SMP-kernel on a UP box... */
		if (atomic_dec_and_test(&__ipipe_critical_count)) {
			spin_unlock(&__ipipe_cpu_barrier);

			while (!cpus_empty(__ipipe_cpu_sync_map))
				cpu_relax();

			cpu_clear(ipipe_processor_id(), __ipipe_cpu_lock_map);
			cpu_clear(BITS_PER_LONG - 1, __ipipe_cpu_lock_map);
		}
	}
#endif	/* CONFIG_SMP */

	local_irq_restore_hw(flags);
}

void __ipipe_init_platform(void)
{
	unsigned int virq;

	/*
	 * Allocate a virtual IRQ for the decrementer trap early to
	 * get it mapped to IPIPE_VIRQ_BASE
	 */

	virq = ipipe_alloc_virq();

	if (virq != IPIPE_TIMER_VIRQ)
		panic("I-pipe: cannot reserve timer virq #%d (got #%d)",
		      IPIPE_TIMER_VIRQ, virq);

#ifdef CONFIG_SMP
	virq = ipipe_alloc_virq();
	if (virq != IPIPE_CRITICAL_IPI)
		panic("I-pipe: cannot reserve critical IPI virq #%d (got #%d)",
		      IPIPE_CRITICAL_IPI, virq);
	virq = ipipe_alloc_virq();
	if (virq != IPIPE_SERVICE_IPI0)
		panic("I-pipe: cannot reserve service IPI 0 virq #%d (got #%d)",
		      IPIPE_SERVICE_IPI0, virq);
	virq = ipipe_alloc_virq();
	if (virq != IPIPE_SERVICE_IPI1)
		panic("I-pipe: cannot reserve service IPI 1 virq #%d (got #%d)",
		      IPIPE_SERVICE_IPI1, virq);
	virq = ipipe_alloc_virq();
	if (virq != IPIPE_SERVICE_IPI2)
		panic("I-pipe: cannot reserve service IPI 2 virq #%d (got #%d)",
		      IPIPE_SERVICE_IPI2, virq);
	virq = ipipe_alloc_virq();
	if (virq != IPIPE_SERVICE_IPI3)
		panic("I-pipe: cannot reserve service IPI 3 virq #%d (got #%d)",
		      IPIPE_SERVICE_IPI3, virq);
	virq = ipipe_alloc_virq();
	if (virq != IPIPE_SERVICE_IPI4)
		panic("I-pipe: cannot reserve service IPI 4 virq #%d (got #%d)",
		      IPIPE_SERVICE_IPI4, virq);
#endif
}

void __ipipe_end_irq(unsigned irq)
{
	irq_desc_t *desc = irq_desc + irq;
	desc->ipipe_end(irq, desc);
}

void __ipipe_enable_irqdesc(struct ipipe_domain *ipd, unsigned irq)
{
	irq_desc[irq].status &= ~IRQ_DISABLED;
}

static void __ipipe_ack_irq(unsigned irq, struct irq_desc *desc)
{
	desc->ipipe_ack(irq, desc);
}

/*
 * __ipipe_enable_pipeline() -- We are running on the boot CPU, hw
 * interrupts are off, and secondary CPUs are still lost in space.
 */
void __ipipe_enable_pipeline(void)
{
	unsigned long flags;
	unsigned irq;

	flags = ipipe_critical_enter(NULL);

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

#ifdef CONFIG_IPIPE_DEBUG
	if (irq >= IPIPE_NR_IRQS ||
	    (ipipe_virtual_irq_p(irq)
	     && !test_bit(irq - IPIPE_VIRQ_BASE, &__ipipe_virtual_irq_map)))
		return -EINVAL;
#endif
	local_irq_save_hw(flags);
	__ipipe_handle_irq(irq, NULL);
	local_irq_restore_hw(flags);

	return 1;
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
	int m_ack;

	/* Software-triggered IRQs do not need any ack. */
	m_ack = (regs == NULL);

#ifdef CONFIG_IPIPE_DEBUG
	if (unlikely(irq >= IPIPE_NR_IRQS)) {
		printk(KERN_ERR "I-pipe: spurious interrupt %d\n", irq);
		return;
	}
#endif
	this_domain = ipipe_current_domain;

	if (unlikely(test_bit(IPIPE_STICKY_FLAG, &this_domain->irqs[irq].control)))
		head = &this_domain->p_link;
	else {
		head = __ipipe_pipeline.next;
		next_domain = list_entry(head, struct ipipe_domain, p_link);
		if (likely(test_bit(IPIPE_WIRED_FLAG, &next_domain->irqs[irq].control))) {
			if (!m_ack && next_domain->irqs[irq].acknowledge)
				next_domain->irqs[irq].acknowledge(irq, irq_desc + irq);
			__ipipe_dispatch_wired(next_domain, irq);
			return;
		}
	}

	/* Ack the interrupt. */

	pos = head;

	while (pos != &__ipipe_pipeline) {
		next_domain = list_entry(pos, struct ipipe_domain, p_link);
		prefetch(next_domain);
		/*
		 * For each domain handling the incoming IRQ, mark it as
		 * pending in its log.
		 */
		if (test_bit(IPIPE_HANDLE_FLAG, &next_domain->irqs[irq].control)) {
			/*
			 * Domains that handle this IRQ are polled for
			 * acknowledging it by decreasing priority order. The
			 * interrupt must be made pending _first_ in the
			 * domain's status flags before the PIC is unlocked.
			 */
			__ipipe_set_irq_pending(next_domain, irq);

			if (!m_ack && next_domain->irqs[irq].acknowledge) {
				next_domain->irqs[irq].acknowledge(irq, irq_desc + irq);
				m_ack = 1;
			}
		}

		/*
		 * If the domain does not want the IRQ to be passed down the
		 * interrupt pipe, exit the loop now.
		 */
		if (!test_bit(IPIPE_PASS_FLAG, &next_domain->irqs[irq].control))
			break;

		pos = next_domain->p_link.next;
	}

	/*
	 * If the interrupt preempted the head domain, then do not
	 * even try to walk the pipeline, unless an interrupt is
	 * pending for it.
	 */
	if (test_bit(IPIPE_AHEAD_FLAG, &this_domain->flags) &&
	    ipipe_head_cpudom_var(irqpend_himask) == 0)
		return;

	/*
	 * Now walk the pipeline, yielding control to the highest
	 * priority domain that has pending interrupt(s) or
	 * immediately to the current domain if the interrupt has been
	 * marked as 'sticky'. This search does not go beyond the
	 * current domain in the pipeline.
	 */

	__ipipe_walk_pipeline(head);
}

int __ipipe_grab_irq(struct pt_regs *regs)
{
	extern int ppc_spurious_interrupts;
	int irq;

	irq = ppc_md.get_irq();
	if (unlikely(irq == NO_IRQ)) {
		ppc_spurious_interrupts++;
		goto root_checks;
	}

	if (likely(irq != NO_IRQ_IGNORE)) {
		ipipe_trace_irq_entry(irq);
#ifdef CONFIG_SMP
		/* Check for cascaded I-pipe IPIs */
		if (irq == __ipipe_ipi_irq) {
			__ipipe_ipi_demux(irq, regs);
			ipipe_trace_irq_exit(irq);
			goto root_checks;
		}
#endif /* CONFIG_SMP */
		__ipipe_handle_irq(irq, regs);
		ipipe_trace_irq_exit(irq);
	}

root_checks:

	if (ipipe_root_domain_p) {
#ifdef CONFIG_PPC_970_NAP
		struct thread_info *ti = current_thread_info();
		/* Emulate the napping check when 100% sure we do run
		 * over the root context. */
		if (test_and_clear_bit(TLF_NAPPING, &ti->local_flags))
			regs->nip = regs->link;
#endif
#ifdef CONFIG_PPC64
		ppc64_runlatch_on();
#endif
		if (!test_bit(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status)))
			return 1;
	}

	return 0;
}

static void __ipipe_do_IRQ(unsigned irq, void *cookie)
{
	struct pt_regs *old_regs;
#ifdef CONFIG_IRQSTACKS
	struct thread_info *curtp, *irqtp;
#endif

	/* Provide a valid register frame, even if not the exact one. */
	old_regs = set_irq_regs(&__raw_get_cpu_var(__ipipe_tick_regs));

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
	timer_interrupt(&__raw_get_cpu_var(__ipipe_tick_regs));
}

int __ipipe_grab_timer(struct pt_regs *regs)
{
	struct ipipe_domain *ipd, *head;

	ipd = ipipe_current_domain;
	head = __ipipe_pipeline_head();

	set_dec(DECREMENTER_MAX);

	ipipe_trace_irq_entry(IPIPE_TIMER_VIRQ);

	__raw_get_cpu_var(__ipipe_tick_regs).msr = regs->msr; /* for timer_interrupt() */
	__raw_get_cpu_var(__ipipe_tick_regs).nip = regs->nip;

	if (ipd != &ipipe_root)
		__raw_get_cpu_var(__ipipe_tick_regs).msr &= ~MSR_EE;

	if (test_bit(IPIPE_WIRED_FLAG, &head->irqs[IPIPE_TIMER_VIRQ].control))
		/*
		 * Finding a wired IRQ means that we do have a
		 * registered head domain as well. The decrementer
		 * interrupt requires no acknowledge, so we may branch
		 * to the wired IRQ dispatcher directly. Additionally,
		 * we may bypass checks for locked interrupts or
		 * stalled stage (the decrementer cannot be locked and
		 * the head domain is obviously not stalled since we
		 * got there).
		 */
		__ipipe_dispatch_wired_nocheck(head, IPIPE_TIMER_VIRQ);
	else
		__ipipe_handle_irq(IPIPE_TIMER_VIRQ, NULL);

	ipipe_trace_irq_exit(IPIPE_TIMER_VIRQ);

	if (ipd == &ipipe_root) {
#ifdef CONFIG_PPC_970_NAP
		struct thread_info *ti = current_thread_info();
		/* Emulate the napping check when 100% sure we do run
		 * over the root context. */
		if (test_and_clear_bit(TLF_NAPPING, &ti->local_flags))
			regs->nip = regs->link;
#endif
#ifdef CONFIG_PPC64
		ppc64_runlatch_on();
#endif
		if (!test_bit(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status)))
			return 1;
	}

	return 0;
}

notrace int __ipipe_check_root(void)
{
	return ipipe_root_domain_p;
}

#ifdef CONFIG_PPC64

#include <asm/firmware.h>
#include <asm/lv1call.h>

notrace void __ipipe_restore_if_root(unsigned long x)
{
	if (likely(!ipipe_root_domain_p))
		return;

	if (x)
		__set_bit(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status));
	else
		__clear_bit(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status));

	if ((int)mfspr(SPRN_DEC) < 0)
		mtspr(SPRN_DEC, 1);

	/*
	 * Force the delivery of pending soft-disabled interrupts on
	 * PS3.  Any HV call will have this side effect.
	 */
	if (firmware_has_feature(FW_FEATURE_PS3_LV1)) {
		u64 tmp;
		lv1_get_version_info(&tmp);
	}

	local_irq_enable_hw();
}

#else

notrace void __ipipe_fast_stall_root(void)
{
	set_bit_safe(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status));
}

notrace void __ipipe_fast_unstall_root(void)
{
	clear_bit_safe(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status));
}

#endif

#ifdef CONFIG_IPIPE_TRACE_IRQSOFF

notrace void __ipipe_trace_irqsoff(void)
{
	ipipe_trace_irqsoff();
}

notrace void __ipipe_trace_irqson(void)
{
	ipipe_trace_irqson();
}

notrace void __ipipe_trace_irqsx(unsigned long msr_ee)
{
	if (msr_ee)
		ipipe_trace_irqson();
	else
		ipipe_trace_irqsoff();
}

#endif

int __ipipe_syscall_root(struct pt_regs *regs) /* HW interrupts off */
{
#ifdef CONFIG_PPC64
	/* We did not copy the stall state on entry yet, so do it now. */
	regs->softe = !test_bit(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status));

	/* We ran DISABLE_INTS before being sent to the syscall
	 * dispatcher, so we need to unstall the root stage, unless
	 * the root domain is not current. */
	if (ipipe_root_domain_p)
		__clear_bit(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status));

	local_irq_enable_hw();
#endif

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
		if (ipipe_root_domain_p && !in_atomic()) {
			/*								     
			 * Sync pending VIRQs before _TIF_NEED_RESCHED			     
			 * is tested.							     
			 */
			local_irq_disable_hw();
			if ((ipipe_root_cpudom_var(irqpend_himask) & IPIPE_IRQMASK_VIRT) != 0)
				__ipipe_sync_pipeline(IPIPE_IRQMASK_VIRT);
			local_irq_enable_hw();
			return -1;
		}
		return 1;
	}

	return 0;
}

void __ipipe_pin_range_globally(unsigned long start, unsigned long end)
{
	/* We don't support this. */
}

#ifdef CONFIG_SMP
EXPORT_SYMBOL(__ipipe_stall_root);
EXPORT_SYMBOL(__ipipe_test_root);
EXPORT_SYMBOL(__ipipe_test_and_stall_root);
#endif

EXPORT_SYMBOL_GPL(__switch_to);
EXPORT_SYMBOL_GPL(show_stack);
EXPORT_SYMBOL_GPL(_switch);
EXPORT_SYMBOL_GPL(tasklist_lock);
#ifdef CONFIG_PPC64
EXPORT_PER_CPU_SYMBOL(ppc64_tlb_batch);
EXPORT_SYMBOL_GPL(switch_slb);
EXPORT_SYMBOL_GPL(switch_stab);
EXPORT_SYMBOL_GPL(__flush_tlb_pending);
EXPORT_SYMBOL_GPL(mmu_linear_psize);
EXPORT_SYMBOL_GPL(mmu_psize_defs);
#else  /* !CONFIG_PPC64 */
void atomic_set_mask(unsigned long mask, unsigned long *ptr);
void atomic_clear_mask(unsigned long mask, unsigned long *ptr);
extern unsigned long context_map[];
#ifdef FEW_CONTEXTS
EXPORT_SYMBOL_GPL(nr_free_contexts);
EXPORT_SYMBOL_GPL(context_mm);
EXPORT_SYMBOL_GPL(steal_context);
#endif	/* !FEW_CONTEXTS */
EXPORT_SYMBOL_GPL(context_map);
EXPORT_SYMBOL_GPL(atomic_set_mask);
EXPORT_SYMBOL_GPL(atomic_clear_mask);
#ifndef CONFIG_SMP
EXPORT_SYMBOL_GPL(last_task_used_math);
#endif
#endif	/* !CONFIG_PPC64 */
#ifdef CONFIG_IPIPE_TRACE_MCOUNT
void notrace _mcount(void);
EXPORT_SYMBOL(_mcount);
#endif /* CONFIG_IPIPE_TRACE_MCOUNT */
