/* -*- linux-c -*-
 * linux/arch/blackfin/kernel/ipipe.c
 *
 * Copyright (C) 2005 Philippe Gerum.
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
 * Architecture-dependent I-pipe support for the Blackfin.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <asm/unistd.h>
#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/io.h>

asmlinkage void asm_do_IRQ(unsigned int irq, struct pt_regs *regs);

extern struct irq_desc irq_desc[];

struct pt_regs __ipipe_irq_regs[IPIPE_NR_CPUS];

static void __ipipe_no_irqtail(void);

unsigned long __ipipe_irq_tail_hook = (unsigned long)&__ipipe_no_irqtail;

unsigned long __ipipe_core_clock;

unsigned long __ipipe_freq_scale;

atomic_t __ipipe_irq_lvdepth[IVG15 + 1];

unsigned long __ipipe_irq_lvmask = __all_masked_irq_flags;

static int __ipipe_ack_irq(unsigned irq)
{
	struct irq_desc *desc = irq_desc + irq;

	if (irq != IRQ_SYSTMR)
		desc->ipipe_ack(irq, desc);

	return 1;
}

/*
 * __ipipe_enable_pipeline() -- We are running on the boot CPU, hw
 * interrupts are off, and secondary CPUs are still lost in space.
 */
void __ipipe_enable_pipeline(void)
{
	unsigned irq;

	__ipipe_core_clock = get_cclk(); /* Fetch this once. */
	__ipipe_freq_scale = 1000000000UL / __ipipe_core_clock;

	for (irq = 0; irq < NR_IRQS; irq++) {
		if (irq != IRQ_SW_INT1 && irq != IRQ_SW_INT2)
			ipipe_virtualize_irq(ipipe_root_domain,
					     irq,
					     (ipipe_irq_handler_t)&asm_do_IRQ,
					     NULL,
					     (ipipe_irq_ackfn_t)&__ipipe_ack_irq,
					     IPIPE_HANDLE_MASK | IPIPE_PASS_MASK);
	}
}

/*
 * __ipipe_handle_irq() -- IPIPE's generic IRQ handler. An optimistic
 * interrupt protection log is maintained here for each domain. Hw
 * interrupts are masked on entry.
 */
void __ipipe_handle_irq(unsigned irq, struct pt_regs *regs)
{
	struct ipipe_domain *this_domain, *next_domain;
	int (*ackfn)(unsigned int irq);
	struct list_head *head, *pos;
	ipipe_declare_cpuid;
	int m_ack, s = -1;

	m_ack = (regs == NULL);	/* Software-triggered IRQs do not need
				 * any ack. */
	ipipe_load_cpuid();

	head = __ipipe_pipeline.next;
	next_domain = list_entry(head, struct ipipe_domain, p_link);
	if (likely(test_bit(IPIPE_WIRED_FLAG, &next_domain->irqs[irq].control))) {
		ackfn = (typeof(ackfn))next_domain->irqs[irq].acknowledge;
		if (!m_ack && ackfn != NULL)
			ackfn(irq);
		if (test_bit(IPIPE_ROOTLOCK_FLAG, &ipipe_root_domain->flags))
			s = __test_and_set_bit(IPIPE_STALL_FLAG,
					       &ipipe_root_domain->cpudata[cpuid].status);
		if (likely(__ipipe_dispatch_wired(next_domain, irq))) {
			ipipe_load_cpuid();
			goto finalize;
		}
		return;
	}

	this_domain = per_cpu(ipipe_percpu_domain, cpuid);

	if (test_bit(IPIPE_STICKY_FLAG, &this_domain->irqs[irq].control))
		head = &this_domain->p_link;

	/* Ack the interrupt. */

	pos = head;

	while (pos != &__ipipe_pipeline) {
		next_domain = list_entry(pos, struct ipipe_domain, p_link);

		/*
		 * For each domain handling the incoming IRQ, mark it
		 * as pending in its log.
		 */
		if (test_bit(IPIPE_HANDLE_FLAG, &next_domain->irqs[irq].control)) {
			/*
			 * Domains that handle this IRQ are polled for
			 * acknowledging it by decreasing priority
			 * order. The interrupt must be made pending
			 * _first_ in the domain's status flags before
			 * the PIC is unlocked.
			 */
			next_domain->cpudata[cpuid].irq_counters[irq].total_hits++;
			next_domain->cpudata[cpuid].irq_counters[irq].pending_hits++;
			__ipipe_set_irq_bit(next_domain, cpuid, irq);
			ackfn = (typeof(ackfn))next_domain->irqs[irq].acknowledge;

			if (!m_ack && ackfn != NULL)
				m_ack = ackfn(irq);
		}

		/*
		 * If the domain does not want the IRQ to be passed
		 * down the interrupt pipe, exit the loop now.
		 */

		if (!test_bit(IPIPE_PASS_FLAG, &next_domain->irqs[irq].control))
			break;

		pos = next_domain->p_link.next;
	}

	/*
	 * Now walk the pipeline, yielding control to the highest
	 * priority domain that has pending interrupt(s) or
	 * immediately to the current domain if the interrupt has been
	 * marked as 'sticky'. This search does not go beyond the
	 * current domain in the pipeline. We also enforce the
	 * additional root stage lock (blackfin-specific). */

	if (test_bit(IPIPE_ROOTLOCK_FLAG, &ipipe_root_domain->flags))
		s = __test_and_set_bit(IPIPE_STALL_FLAG,
				       &ipipe_root_domain->cpudata[cpuid].status);
finalize:

	__ipipe_walk_pipeline(head, cpuid);

	if (!s)
		__clear_bit(IPIPE_STALL_FLAG,
			    &ipipe_root_domain->cpudata[cpuid].status);
}

int __ipipe_check_root(void)
{
	ipipe_declare_cpuid;
	/*
	 * SMP: This routine is called with hw interrupts off, so no
	 * migration can occur while checking the identity of the
	 * current domain.
	 */
	ipipe_load_cpuid();
	return per_cpu(ipipe_percpu_domain, cpuid) == ipipe_root_domain;
}

void __ipipe_stall_root_raw(void)
{
	ipipe_declare_cpuid;

	/* This code is called by the ins{bwl} routines (see
	   arch/blackfin/lib/ins.S), which are heavily used by the
	   network stack. It masks all interrupts but those handled by
	   non-root domains, so that we keep decent network transfer
	   rates for Linux without inducing pathological jitter for
	   the real-time domain. */
	__asm__ __volatile__ ("sti %0;"::"d"(__ipipe_irq_lvmask));

	ipipe_load_cpuid();

	__set_bit(IPIPE_STALL_FLAG,
		  &ipipe_root_domain->cpudata[cpuid].status);
}

void __ipipe_unstall_root_raw(void)
{
	ipipe_declare_cpuid;

	ipipe_load_cpuid();

	__clear_bit(IPIPE_STALL_FLAG,
		    &ipipe_root_domain->cpudata[cpuid].status);

	__asm__ __volatile__ ("sti %0;"::"d"(irq_flags));
}

int __ipipe_syscall_root(struct pt_regs *regs)
{
	ipipe_declare_cpuid;
	unsigned long flags;

	/* We need to run the IRQ tail hook whenever we don't
	 * propagate a syscall to higher domains, because we know that
	 * important operations might be pending there (e.g. Xenomai
	 * deferred rescheduling). */

	if (!__ipipe_syscall_watched_p(current, regs->orig_p0)) {
		void (*hook)(void) = (void (*)(void))__ipipe_irq_tail_hook;
		hook();
		return 0;
	}

	/*
	 * This routine either returns:
	 * 0 -- if the syscall is to be passed to Linux;
	 * 1 -- if the syscall should not be passed to Linux, and no
	 * tail work should be performed;
	 * -1 -- if the syscall should not be passed to Linux but the
	 * tail work has to be performed (for handling signals etc).
	 */

	if (__ipipe_event_monitored_p(IPIPE_EVENT_SYSCALL) &&
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

#ifdef CONFIG_SMP

static cpumask_t __ipipe_cpu_sync_map;

static cpumask_t __ipipe_cpu_lock_map;

static IPIPE_DEFINE_SPINLOCK(__ipipe_cpu_barrier);

static atomic_t __ipipe_critical_count = ATOMIC_INIT(0);

static void (*__ipipe_cpu_sync) (void);

/* Always called with hw interrupts off. */

void __ipipe_do_critical_sync(unsigned irq)
{
	ipipe_declare_cpuid;

	ipipe_load_cpuid();

	cpu_set(cpuid, __ipipe_cpu_sync_map);

	/* Now we are in sync with the lock requestor running on another
	   CPU. Enter a spinning wait until he releases the global
	   lock. */
	spin_lock(&__ipipe_cpu_barrier);

	/* Got it. Now get out. */

	if (__ipipe_cpu_sync)
		/* Call the sync routine if any. */
		__ipipe_cpu_sync();

	spin_unlock(&__ipipe_cpu_barrier);

	cpu_clear(cpuid, __ipipe_cpu_sync_map);
}

#endif	/* CONFIG_SMP */

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
			while (cpu_test_and_set
			       (BITS_PER_LONG - 1, __ipipe_cpu_lock_map)) {
				int n = 0;
				do {
					cpu_relax();
				} while (++n < cpuid);
			}

			spin_lock(&__ipipe_cpu_barrier);

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

void ipipe_critical_exit(unsigned long flags)
{
#ifdef CONFIG_SMP
	if (num_online_cpus() > 1) {	/* We might be running a SMP-kernel on a UP box... */
		ipipe_declare_cpuid;

		ipipe_load_cpuid();

		if (atomic_dec_and_test(&__ipipe_critical_count)) {
			spin_unlock(&__ipipe_cpu_barrier);

			while (!cpus_empty(__ipipe_cpu_sync_map))
				cpu_relax();

			cpu_clear(cpuid, __ipipe_cpu_lock_map);
			cpu_clear(BITS_PER_LONG - 1, __ipipe_cpu_lock_map);
		}
	}
#endif	/* CONFIG_SMP */

	local_irq_restore_hw(flags);
}

static void __ipipe_no_irqtail(void)
{
}

int ipipe_get_sysinfo(struct ipipe_sysinfo *info)
{
	info->ncpus = num_online_cpus();
	info->cpufreq = ipipe_cpu_freq();
	info->archdep.tmirq = IPIPE_TIMER_IRQ;
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

int ipipe_tune_timer(unsigned long ns, int flags)
{
	unsigned long x, hz;

	x = ipipe_critical_enter(NULL);

	bfin_write_TIMER_DISABLE(1);	
	__builtin_bfin_ssync();
	bfin_write_TIMER0_CONFIG(0x59);	/* IRQ enable, periodic, PWM_OUT, SCLKed, OUT PAD disabled */
	__builtin_bfin_ssync();
	hz = (flags & IPIPE_RESET_TIMER) ? HZ : 1000000000L / ns;
	bfin_write_TIMER0_PERIOD(get_sclk() / hz);
	__builtin_bfin_ssync();
	bfin_write_TIMER0_WIDTH(1);
	__builtin_bfin_ssync();
	bfin_write_TIMER_ENABLE(1);
	__builtin_bfin_ssync();

	ipipe_critical_exit(x);

	return 0;
}

EXPORT_SYMBOL(__ipipe_irq_tail_hook);
EXPORT_SYMBOL(ipipe_critical_enter);
EXPORT_SYMBOL(ipipe_critical_exit);
EXPORT_SYMBOL(ipipe_trigger_irq);
EXPORT_SYMBOL(ipipe_get_sysinfo);
EXPORT_SYMBOL(ipipe_tune_timer);

EXPORT_SYMBOL(__ipipe_core_clock);
EXPORT_SYMBOL(__ipipe_freq_scale);
EXPORT_SYMBOL(show_stack);

#ifdef CONFIG_IPIPE_TRACE_MCOUNT
void notrace _mcount(void);
EXPORT_SYMBOL(_mcount);
#endif /* CONFIG_IPIPE_TRACE_MCOUNT */
