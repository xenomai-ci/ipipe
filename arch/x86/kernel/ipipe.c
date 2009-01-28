/*   -*- linux-c -*-
 *   linux/arch/x86/kernel/ipipe.c
 *
 *   Copyright (C) 2002-2007 Philippe Gerum.
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
 *
 *   Architecture-dependent I-PIPE support for x86.
 */

#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/clockchips.h>
#include <asm/unistd.h>
#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/hw_irq.h>
#include <asm/irq.h>
#include <asm/desc.h>
#include <asm/io.h>
#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/tlbflush.h>
#include <asm/fixmap.h>
#include <asm/bitops.h>
#include <asm/mpspec.h>
#ifdef CONFIG_X86_IO_APIC
#include <asm/io_apic.h>
#endif	/* CONFIG_X86_IO_APIC */
#include <asm/apic.h>
#include <mach_ipi.h>
#endif	/* CONFIG_X86_LOCAL_APIC */
#include <asm/traps.h>

int __ipipe_tick_irq = 0;	/* Legacy timer */

DEFINE_PER_CPU(struct pt_regs, __ipipe_tick_regs);

#ifdef CONFIG_SMP

static cpumask_t __ipipe_cpu_sync_map;

static cpumask_t __ipipe_cpu_lock_map;

static IPIPE_DEFINE_SPINLOCK(__ipipe_cpu_barrier);

static atomic_t __ipipe_critical_count = ATOMIC_INIT(0);

static void (*__ipipe_cpu_sync) (void);

#endif /* CONFIG_SMP */

/* ipipe_trigger_irq() -- Push the interrupt at front of the pipeline
   just like if it has been actually received from a hw source. Also
   works for virtual interrupts. */

int ipipe_trigger_irq(unsigned irq)
{
	struct pt_regs regs;
	unsigned long flags;

	if (irq >= IPIPE_NR_IRQS ||
	    (ipipe_virtual_irq_p(irq) &&
	     !test_bit(irq - IPIPE_VIRQ_BASE, &__ipipe_virtual_irq_map)))
		return -EINVAL;

	local_irq_save_hw(flags);

	regs.flags = flags;
#ifdef CONFIG_X86_32
	regs.orig_ax = irq;	/* Positive value - IRQ won't be acked */
	regs.cs = __KERNEL_CS;

	__ipipe_handle_irq(regs);

#else /* !CONFIG_X86_32 */
	regs.orig_ax = irq;	/* Positive value - IRQ won't be acked */
	regs.cs = __KERNEL_CS;

	__ipipe_handle_irq(&regs);
#endif /* !CONFIG_X86_32 */

	local_irq_restore_hw(flags);

	return 1;
}

int ipipe_get_sysinfo(struct ipipe_sysinfo *info)
{
	info->ncpus = num_online_cpus();
	info->cpufreq = ipipe_cpu_freq();
	info->archdep.tmirq = __ipipe_tick_irq;
#ifdef CONFIG_X86_TSC
	info->archdep.tmfreq = ipipe_cpu_freq();
#else	/* !CONFIG_X86_TSC */
	info->archdep.tmfreq = CLOCK_TICK_RATE;
#endif	/* CONFIG_X86_TSC */

	return 0;
}

unsigned int do_IRQ(struct pt_regs *regs);
void smp_apic_timer_interrupt(struct pt_regs *regs);
void smp_spurious_interrupt(struct pt_regs *regs);
void smp_error_interrupt(struct pt_regs *regs);
void smp_thermal_interrupt(struct pt_regs *regs);
void smp_reschedule_interrupt(struct pt_regs *regs);
void smp_invalidate_interrupt(struct pt_regs *regs);
void smp_call_function_interrupt(struct pt_regs *regs);
void smp_call_function_single_interrupt(struct pt_regs *regs);
void mce_threshold_interrupt(struct pt_regs *regs);
void uv_bau_message_interrupt(struct pt_regs *regs);

static void __ipipe_ack_irq(unsigned irq, struct irq_desc *desc)
{
	desc->ipipe_ack(irq, desc);
}

void __ipipe_enable_irqdesc(struct ipipe_domain *ipd, unsigned irq)
{
	irq_to_desc(irq)->status &= ~IRQ_DISABLED;
}

#ifdef CONFIG_X86_LOCAL_APIC

static void __ipipe_noack_apic(unsigned irq, struct irq_desc *desc)
{
}

static void __ipipe_ack_apic(unsigned irq, struct irq_desc *desc)
{
	__ack_APIC_irq();
}

static void __ipipe_null_handler(unsigned irq, void *cookie)
{
}

#endif	/* CONFIG_X86_LOCAL_APIC */

/* __ipipe_enable_pipeline() -- We are running on the boot CPU, hw
   interrupts are off, and secondary CPUs are still lost in space. */

void __init __ipipe_enable_pipeline(void)
{
	unsigned irq;

#ifdef CONFIG_X86_LOCAL_APIC

	/* Map the APIC system vectors. */

	ipipe_virtualize_irq(ipipe_root_domain,
			     ipipe_apic_vector_irq(LOCAL_TIMER_VECTOR),
			     (ipipe_irq_handler_t)&smp_apic_timer_interrupt,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

	ipipe_virtualize_irq(ipipe_root_domain,
			     ipipe_apic_vector_irq(SPURIOUS_APIC_VECTOR),
			     (ipipe_irq_handler_t)&smp_spurious_interrupt,
			     NULL,
			     &__ipipe_noack_apic,
			     IPIPE_STDROOT_MASK);

	ipipe_virtualize_irq(ipipe_root_domain,
			     ipipe_apic_vector_irq(ERROR_APIC_VECTOR),
			     (ipipe_irq_handler_t)&smp_error_interrupt,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

	ipipe_virtualize_irq(ipipe_root_domain,
			     ipipe_apic_vector_irq(IPIPE_SERVICE_VECTOR0),
			     &__ipipe_null_handler,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

	ipipe_virtualize_irq(ipipe_root_domain,
			     ipipe_apic_vector_irq(IPIPE_SERVICE_VECTOR1),
			     &__ipipe_null_handler,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

	ipipe_virtualize_irq(ipipe_root_domain,
			     ipipe_apic_vector_irq(IPIPE_SERVICE_VECTOR2),
			     &__ipipe_null_handler,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

	ipipe_virtualize_irq(ipipe_root_domain,
			     ipipe_apic_vector_irq(IPIPE_SERVICE_VECTOR3),
			     &__ipipe_null_handler,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

#if defined(CONFIG_X86_MCE_P4THERMAL) || defined(CONFIG_X86_64)
	ipipe_virtualize_irq(ipipe_root_domain,
			     ipipe_apic_vector_irq(THERMAL_APIC_VECTOR),
			     (ipipe_irq_handler_t)&smp_thermal_interrupt,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);
#endif /* CONFIG_X86_MCE_P4THERMAL || CONFIG_X86_64 */

#ifdef CONFIG_X86_64
	ipipe_virtualize_irq(ipipe_root_domain,
			     ipipe_apic_vector_irq(THRESHOLD_APIC_VECTOR),
			     (ipipe_irq_handler_t)&mce_threshold_interrupt,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

	ipipe_virtualize_irq(ipipe_root_domain,
			     ipipe_apic_vector_irq(UV_BAU_MESSAGE),
			     (ipipe_irq_handler_t)&uv_bau_message_interrupt,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);
#endif /* CONFIG_X86_64 */

#endif	/* CONFIG_X86_LOCAL_APIC */

#ifdef CONFIG_SMP
	ipipe_virtualize_irq(ipipe_root_domain,
			     ipipe_apic_vector_irq(RESCHEDULE_VECTOR),
			     (ipipe_irq_handler_t)&smp_reschedule_interrupt,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

#ifdef CONFIG_X86_32
	ipipe_virtualize_irq(ipipe_root_domain,
			     ipipe_apic_vector_irq(INVALIDATE_TLB_VECTOR),
			     (ipipe_irq_handler_t)&smp_invalidate_interrupt,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);
#else /* !CONFIG_X86_32 */
	{
		unsigned vector;

		for (vector = INVALIDATE_TLB_VECTOR_START;
		     vector <= INVALIDATE_TLB_VECTOR_END; ++vector)
			ipipe_virtualize_irq(ipipe_root_domain,
					     ipipe_apic_vector_irq(vector),
					     (ipipe_irq_handler_t)&smp_invalidate_interrupt,
					     NULL,
					     &__ipipe_ack_apic,
					     IPIPE_STDROOT_MASK);
	}
#endif /* !CONFIG_X86_32 */

	ipipe_virtualize_irq(ipipe_root_domain,
			     ipipe_apic_vector_irq(CALL_FUNCTION_VECTOR),
			     (ipipe_irq_handler_t)&smp_call_function_interrupt,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

	ipipe_virtualize_irq(ipipe_root_domain,
			     ipipe_apic_vector_irq(CALL_FUNCTION_SINGLE_VECTOR),
			     (ipipe_irq_handler_t)&smp_call_function_single_interrupt,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

#endif	/* CONFIG_SMP */

	/* Finally, virtualize the remaining ISA and IO-APIC
	 * interrupts. Interrupts which have already been virtualized
	 * will just beget a silent -EPERM error since
	 * IPIPE_SYSTEM_MASK has been passed for them, that's ok. */

	for (irq = 0; irq < NR_IRQS; irq++)
		/* Fails for IPIPE_CRITICAL_IPI but that's ok. */
		ipipe_virtualize_irq(ipipe_root_domain,
				     irq,
				     (ipipe_irq_handler_t)&do_IRQ,
				     NULL,
				     &__ipipe_ack_irq,
				     IPIPE_STDROOT_MASK);

#ifdef CONFIG_X86_LOCAL_APIC
	/* Eventually allow these vectors to be reprogrammed. */
	ipipe_root_domain->irqs[IPIPE_SERVICE_IPI0].control &= ~IPIPE_SYSTEM_MASK;
	ipipe_root_domain->irqs[IPIPE_SERVICE_IPI1].control &= ~IPIPE_SYSTEM_MASK;
	ipipe_root_domain->irqs[IPIPE_SERVICE_IPI2].control &= ~IPIPE_SYSTEM_MASK;
	ipipe_root_domain->irqs[IPIPE_SERVICE_IPI3].control &= ~IPIPE_SYSTEM_MASK;
#endif	/* CONFIG_X86_LOCAL_APIC */
}

#ifdef CONFIG_SMP

cpumask_t __ipipe_set_irq_affinity(unsigned irq, cpumask_t cpumask)
{
	cpumask_t oldmask = irq_to_desc(irq)->affinity;

	if (irq_to_desc(irq)->chip->set_affinity == NULL)
		return CPU_MASK_NONE;

	if (cpus_empty(cpumask))
		return oldmask; /* Return mask value -- no change. */

	cpus_and(cpumask,cpumask,cpu_online_map);

	if (cpus_empty(cpumask))
		return CPU_MASK_NONE;	/* Error -- bad mask value or non-routable IRQ. */

	irq_to_desc(irq)->chip->set_affinity(irq,cpumask);

	return oldmask;
}

int __ipipe_send_ipi(unsigned ipi, cpumask_t cpumask)
{
	unsigned long flags;
	int self;

	if (ipi != IPIPE_SERVICE_IPI0 &&
	    ipi != IPIPE_SERVICE_IPI1 &&
	    ipi != IPIPE_SERVICE_IPI2 &&
	    ipi != IPIPE_SERVICE_IPI3)
		return -EINVAL;

	local_irq_save_hw(flags);

	self = cpu_isset(ipipe_processor_id(),cpumask);
	cpu_clear(ipipe_processor_id(), cpumask);

	if (!cpus_empty(cpumask))
		send_IPI_mask(cpumask, ipipe_apic_irq_vector(ipi));

	if (self)
		ipipe_trigger_irq(ipi);

	local_irq_restore_hw(flags);

	return 0;
}

/* Always called with hw interrupts off. */

void __ipipe_do_critical_sync(unsigned irq, void *cookie)
{
	int cpu = ipipe_processor_id();

	cpu_set(cpu, __ipipe_cpu_sync_map);

	/* Now we are in sync with the lock requestor running on another
	   CPU. Enter a spinning wait until he releases the global
	   lock. */
	spin_lock(&__ipipe_cpu_barrier);

	/* Got it. Now get out. */

	if (__ipipe_cpu_sync)
		/* Call the sync routine if any. */
		__ipipe_cpu_sync();

	spin_unlock(&__ipipe_cpu_barrier);

	cpu_clear(cpu, __ipipe_cpu_sync_map);
}

void __ipipe_hook_critical_ipi(struct ipipe_domain *ipd)
{
	ipd->irqs[IPIPE_CRITICAL_IPI].acknowledge = &__ipipe_ack_apic;
	ipd->irqs[IPIPE_CRITICAL_IPI].handler = &__ipipe_do_critical_sync;
	ipd->irqs[IPIPE_CRITICAL_IPI].cookie = NULL;
	/* Immediately handle in the current domain but *never* pass */
	ipd->irqs[IPIPE_CRITICAL_IPI].control =
		IPIPE_HANDLE_MASK|IPIPE_STICKY_MASK|IPIPE_SYSTEM_MASK;
}

#endif	/* CONFIG_SMP */

/* ipipe_critical_enter() -- Grab the superlock excluding all CPUs
   but the current one from a critical section. This lock is used when
   we must enforce a global critical section for a single CPU in a
   possibly SMP system whichever context the CPUs are running. */

unsigned long ipipe_critical_enter(void (*syncfn) (void))
{
	unsigned long flags;

	local_irq_save_hw(flags);

#ifdef CONFIG_SMP
	if (unlikely(num_online_cpus() == 1))	/* We might be running a SMP-kernel on a UP box... */
		return flags;

	{
		int cpu = ipipe_processor_id();
		cpumask_t lock_map;

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
			send_IPI_allbutself(IPIPE_CRITICAL_VECTOR);

			cpus_andnot(lock_map, cpu_online_map, __ipipe_cpu_lock_map);

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
	if (num_online_cpus() == 1)
		goto out;

	if (atomic_dec_and_test(&__ipipe_critical_count)) {
		spin_unlock(&__ipipe_cpu_barrier);

		while (!cpus_empty(__ipipe_cpu_sync_map))
			cpu_relax();

		cpu_clear(ipipe_processor_id(), __ipipe_cpu_lock_map);
		cpu_clear(BITS_PER_LONG - 1, __ipipe_cpu_lock_map);
	}
out:
#endif	/* CONFIG_SMP */

	local_irq_restore_hw(flags);
}

static inline void __fixup_if(struct pt_regs *regs)
{
	if (!ipipe_root_domain_p)
		return;

	/*
	 * Have the saved hw state look like the domain stall bit, so
	 * that __ipipe_unstall_iret_root() restores the proper
	 * pipeline state for the root stage upon exit.
	 */

	if (test_bit(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status)))
		regs->flags &= ~X86_EFLAGS_IF;
	else
		regs->flags |= X86_EFLAGS_IF;
}

#ifdef CONFIG_X86_32

/*
 * Check the stall bit of the root domain to make sure the existing
 * preemption opportunity upon in-kernel resumption could be
 * exploited. In case a rescheduling could take place, the root stage
 * is stalled before the hw interrupts are re-enabled. This routine
 * must be called with hw interrupts off.
 */

asmlinkage int __ipipe_kpreempt_root(struct pt_regs regs)
{
	if (test_bit(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status)))
		/* Root stage is stalled: rescheduling denied. */
		return 0;

	__ipipe_stall_root();
	local_irq_enable_hw_notrace();

	return 1;	/* Ok, may reschedule now. */
}

asmlinkage void __ipipe_unstall_iret_root(struct pt_regs regs)
{
	struct ipipe_percpu_domain_data *p = ipipe_root_cpudom_ptr();

	/* Emulate IRET's handling of the interrupt flag. */

	local_irq_disable_hw();

	/* Restore the software state as it used to be on kernel
	   entry. CAUTION: NMIs must *not* return through this
	   emulation. */

	if (!(regs.flags & X86_EFLAGS_IF)) {
		if (!__test_and_set_bit(IPIPE_STALL_FLAG, &p->status))
			trace_hardirqs_off();
		regs.flags |= X86_EFLAGS_IF;
	} else {
		if (test_bit(IPIPE_STALL_FLAG, &p->status)) {
			trace_hardirqs_on();
			__clear_bit(IPIPE_STALL_FLAG, &p->status);
		}

		/* Only sync virtual IRQs here, so that we don't recurse
		   indefinitely in case of an external interrupt flood. */

		if ((p->irqpend_himask & IPIPE_IRQMASK_VIRT) != 0)
			__ipipe_sync_pipeline(IPIPE_IRQMASK_VIRT);
	}
#ifdef CONFIG_IPIPE_TRACE_IRQSOFF
	ipipe_trace_end(0x8000000D);
#endif /* CONFIG_IPIPE_TRACE_IRQSOFF */
}

asmlinkage int __ipipe_syscall_root(struct pt_regs regs)
{
	unsigned long flags;

	__fixup_if(&regs);

	/* This routine either returns:
	    0 -- if the syscall is to be passed to Linux;
	   >0 -- if the syscall should not be passed to Linux, and no
	   tail work should be performed;
	   <0 -- if the syscall should not be passed to Linux but the
	   tail work has to be performed (for handling signals etc). */

	if (__ipipe_syscall_watched_p(current, regs.orig_ax) &&
	    __ipipe_event_monitored_p(IPIPE_EVENT_SYSCALL) &&
	    __ipipe_dispatch_event(IPIPE_EVENT_SYSCALL,&regs) > 0) {
		/* We might enter here over a non-root domain and exit
		 * over the root one as a result of the syscall
		 * (i.e. by recycling the register set of the current
		 * context across the migration), so we need to fixup
		 * the interrupt flag upon return too, so that
		 * __ipipe_unstall_iret_root() resets the correct
		 * stall bit on exit. */
		__fixup_if(&regs);

		if (ipipe_root_domain_p && !in_atomic()) {
			/* Sync pending VIRQs before _TIF_NEED_RESCHED is tested. */
			local_irq_save_hw(flags);
			if ((ipipe_root_cpudom_var(irqpend_himask) & IPIPE_IRQMASK_VIRT) != 0)
				__ipipe_sync_pipeline(IPIPE_IRQMASK_VIRT);
			local_irq_restore_hw(flags);
			return -1;
		}
		return 1;
	}

	return 0;
}

#else /* !CONFIG_X86_32 */

#ifdef CONFIG_PREEMPT

asmlinkage void preempt_schedule_irq(void);

/*
 * Check the stall bit of the root domain to make sure the existing
 * preemption opportunity upon in-kernel resumption could be
 * exploited. In case a rescheduling could take place, the root stage
 * is stalled before the hw interrupts are re-enabled. This routine
 * must be called with hw interrupts off.
 */
asmlinkage int __ipipe_preempt_schedule_irq(void)
{
	struct ipipe_percpu_domain_data *p = ipipe_root_cpudom_ptr();

	if (test_bit(IPIPE_STALL_FLAG, &p->status))
		/* Root stage is stalled: rescheduling denied. */
		return 0;

	__set_bit(IPIPE_STALL_FLAG, &p->status);
	local_irq_enable_hw();
	preempt_schedule_irq(); /* Ok, may reschedule now. */
	local_irq_disable_hw();
	__clear_bit(IPIPE_STALL_FLAG, &p->status);

	return 1;
}
#endif

asmlinkage int __ipipe_syscall_root(struct pt_regs *regs)
{
	unsigned long flags;

	__fixup_if(regs);

	/* This routine either returns:
	    0 -- if the syscall is to be passed to Linux;
	   >0 -- if the syscall should not be passed to Linux, and no
	   tail work should be performed;
	   <0 -- if the syscall should not be passed to Linux but the
	   tail work has to be performed (for handling signals etc). */

	if (__ipipe_syscall_watched_p(current, regs->orig_ax) &&
	    __ipipe_event_monitored_p(IPIPE_EVENT_SYSCALL) &&
	    __ipipe_dispatch_event(IPIPE_EVENT_SYSCALL, regs) > 0) {
		/* We might enter here over a non-root domain and exit
		 * over the root one as a result of the syscall
		 * (i.e. by recycling the register set of the current
		 * context across the migration), so we need to fixup
		 * the interrupt flag upon return too, so that
		 * __ipipe_unstall_iret_root() resets the correct
		 * stall bit on exit. */
		__fixup_if(regs);

		if (ipipe_root_domain_p && !in_atomic()) {
			/* Sync pending VIRQs before _TIF_NEED_RESCHED is tested. */
			local_irq_save_hw(flags);
			if ((ipipe_root_cpudom_var(irqpend_himask) & IPIPE_IRQMASK_VIRT) != 0)
				__ipipe_sync_pipeline(IPIPE_IRQMASK_VIRT);
			local_irq_restore_hw(flags);
			return -1;
		}
		/*
		 * We are about to run the atomic syscall epilogue;
		 * switch interrupts off before branching to it.
		 */
		local_irq_disable_hw();
		return 1;
	}

    return 0;
}

#endif /* !CONFIG_X86_32 */

static void do_machine_check_vector(struct pt_regs *regs, long error_code)
{
#ifdef CONFIG_X86_MCE
	extern void (*machine_check_vector)(struct pt_regs *, long error_code);
	machine_check_vector(regs, error_code);
#endif /* CONFIG_X86_MCE */
}

/* Work around genksyms's issue with over-qualification in decls. */

typedef void dotraplinkage __ipipe_exhandler(struct pt_regs *, long);

typedef __ipipe_exhandler *__ipipe_exptr;

static __ipipe_exptr __ipipe_std_extable[] = {

	[ex_do_divide_error] = &do_divide_error,
	[ex_do_overflow] = &do_overflow,
	[ex_do_bounds] = &do_bounds,
	[ex_do_invalid_op] = &do_invalid_op,
	[ex_do_coprocessor_segment_overrun] = &do_coprocessor_segment_overrun,
	[ex_do_invalid_TSS] = &do_invalid_TSS,
	[ex_do_segment_not_present] = &do_segment_not_present,
	[ex_do_stack_segment] = &do_stack_segment,
	[ex_do_general_protection] = do_general_protection,
	[ex_do_page_fault] = (__ipipe_exptr)&do_page_fault,
	[ex_do_spurious_interrupt_bug] = &do_spurious_interrupt_bug,
	[ex_do_coprocessor_error] = &do_coprocessor_error,
	[ex_do_alignment_check] = &do_alignment_check,
	[ex_machine_check_vector] = &do_machine_check_vector,
	[ex_do_simd_coprocessor_error] = &do_simd_coprocessor_error,
	[ex_do_device_not_available] = &do_device_not_available,
#ifdef CONFIG_X86_32
	[ex_do_iret_error] = &do_iret_error,
#endif
};

#ifdef CONFIG_KGDB
#include <linux/kgdb.h>

static int __ipipe_xlate_signo[] = {

	[ex_do_divide_error] = SIGFPE,
	[ex_do_debug] = SIGTRAP,
	[2] = -1,
	[ex_do_int3] = SIGTRAP,
	[ex_do_overflow] = SIGSEGV,
	[ex_do_bounds] = SIGSEGV,
	[ex_do_invalid_op] = SIGILL,
	[ex_do_device_not_available] = -1,
	[8] = -1,
	[ex_do_coprocessor_segment_overrun] = SIGFPE,
	[ex_do_invalid_TSS] = SIGSEGV,
	[ex_do_segment_not_present] = SIGBUS,
	[ex_do_stack_segment] = SIGBUS,
	[ex_do_general_protection] = SIGSEGV,
	[ex_do_page_fault] = SIGSEGV,
	[ex_do_spurious_interrupt_bug] = -1,
	[ex_do_coprocessor_error] = -1,
	[ex_do_alignment_check] = SIGBUS,
	[ex_machine_check_vector] = -1,
	[ex_do_simd_coprocessor_error] = -1,
	[20 ... 31] = -1,
#ifdef CONFIG_X86_32
	[ex_do_iret_error] = SIGSEGV,
#endif
};
#endif /* CONFIG_KGDB */

int __ipipe_handle_exception(struct pt_regs *regs, long error_code, int vector)
{
	unsigned long flags;

	local_save_flags(flags);

	/* Track the hw interrupt state before calling the Linux
	 * exception handler, replicating it into the virtual mask. */

	if (irqs_disabled_hw()) {
		/* Do not trigger the alarm in ipipe_check_context() by using
		 * plain local_irq_disable(). */
		__ipipe_stall_root();
		trace_hardirqs_off();
		barrier();
	}

#ifdef CONFIG_KGDB
	/* catch exception KGDB is interested in over non-root domains */
	if (!ipipe_root_domain_p &&
	    __ipipe_xlate_signo[vector] >= 0 &&
	    !kgdb_handle_exception(vector, __ipipe_xlate_signo[vector], error_code, regs)) {
		if (flags & X86_EFLAGS_IF)
			__clear_bit(IPIPE_STALL_FLAG,
				    &ipipe_root_cpudom_var(status));
		return 1;
	}
#endif /* CONFIG_KGDB */

	if (unlikely(ipipe_trap_notify(vector, regs))) {
		if (flags & X86_EFLAGS_IF)
			__clear_bit(IPIPE_STALL_FLAG,
				    &ipipe_root_cpudom_var(status));
		return 1;
	}

	/* Detect unhandled faults over non-root domains. */

	if (unlikely(!ipipe_root_domain_p)) {
		struct ipipe_domain *ipd = ipipe_current_domain;

		/* Switch to root so that Linux can handle the fault cleanly. */
		ipipe_current_domain = ipipe_root_domain;

		ipipe_trace_panic_freeze();

		/* Always warn about user land and unfixable faults. */
		if ((error_code & 4) || !search_exception_tables(instruction_pointer(regs))) {
			printk(KERN_ERR "BUG: Unhandled exception over domain"
			       " %s at 0x%lx - switching to ROOT\n",
			       ipd->name, instruction_pointer(regs));
			dump_stack();
			ipipe_trace_panic_dump();
#ifdef CONFIG_IPIPE_DEBUG
		/* Also report fixable ones when debugging is enabled. */
		} else {
			printk(KERN_WARNING "WARNING: Fixable exception over "
			       "domain %s at 0x%lx - switching to ROOT\n",
			       ipd->name, instruction_pointer(regs));
			dump_stack();
			ipipe_trace_panic_dump();
#endif /* CONFIG_IPIPE_DEBUG */
		}
	}

	__ipipe_std_extable[vector](regs, error_code);
	local_irq_disable_hw();
	local_irq_restore(flags);
	__fixup_if(regs);

	return 0;
}

int __ipipe_divert_exception(struct pt_regs *regs, int vector)
{
#ifdef CONFIG_KGDB
	/* catch int1 and int3 over non-root domains */
	if (!ipipe_root_domain_p && vector != ex_device_not_available) {
		unsigned int condition = 0;

		if (vector == 1)
			get_debugreg(condition, 6);
		if (!kgdb_handle_exception(vector, SIGTRAP, condition, regs))
			return 1;
	}
#endif /* CONFIG_KGDB */

	if (ipipe_trap_notify(vector, regs))
		return 1;

	__fixup_if(regs);

	return 0;
}

/* __ipipe_handle_irq() -- IPIPE's generic IRQ handler. An optimistic
   interrupt protection log is maintained here for each domain.  Hw
   interrupts are off on entry. */

#ifdef CONFIG_X86_32
int __ipipe_handle_irq(struct pt_regs __regs)
{
	struct pt_regs *regs = &__regs;
#else /* !CONFIG_X86_32 */
int __ipipe_handle_irq(struct pt_regs *regs)
{
#endif /* !CONFIG_X86_32 */
	struct ipipe_domain *this_domain, *next_domain;
	unsigned vector = regs->orig_ax, irq;
	struct list_head *head, *pos;
	int m_ack;

	if ((long)regs->orig_ax < 0) {
		vector = ~vector;
#ifdef CONFIG_X86_LOCAL_APIC
		if (vector >= FIRST_SYSTEM_VECTOR)
			irq = ipipe_apic_vector_irq(vector);
		else
#endif
			irq = __get_cpu_var(vector_irq)[vector];
		m_ack = 0;
	} else { /* This is a self-triggered one. */
		irq = vector;
		m_ack = 1;
	}

	this_domain = ipipe_current_domain;

	if (test_bit(IPIPE_STICKY_FLAG, &this_domain->irqs[irq].control))
		head = &this_domain->p_link;
	else {
		head = __ipipe_pipeline.next;
		next_domain = list_entry(head, struct ipipe_domain, p_link);
		if (likely(test_bit(IPIPE_WIRED_FLAG, &next_domain->irqs[irq].control))) {
			if (!m_ack && next_domain->irqs[irq].acknowledge)
				next_domain->irqs[irq].acknowledge(irq, irq_desc + irq);
			__ipipe_dispatch_wired(next_domain, irq);
			goto finalize_nosync;
		}
	}

	/* Ack the interrupt. */

	pos = head;

	while (pos != &__ipipe_pipeline) {
		next_domain = list_entry(pos, struct ipipe_domain, p_link);
		if (test_bit(IPIPE_HANDLE_FLAG, &next_domain->irqs[irq].control)) {
			__ipipe_set_irq_pending(next_domain, irq);
			if (!m_ack && next_domain->irqs[irq].acknowledge) {
				next_domain->irqs[irq].acknowledge(irq, irq_desc + irq);
				m_ack = 1;
			}
		}
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
		goto finalize_nosync;

	/*
	 * Now walk the pipeline, yielding control to the highest
	 * priority domain that has pending interrupt(s) or
	 * immediately to the current domain if the interrupt has been
	 * marked as 'sticky'. This search does not go beyond the
	 * current domain in the pipeline.
	 */

	__ipipe_walk_pipeline(head);

finalize_nosync:

	/*
	 * Given our deferred dispatching model for regular IRQs, we
	 * only record CPU regs for the last timer interrupt, so that
	 * the timer handler charges CPU times properly. It is assumed
	 * that other interrupt handlers don't actually care for such
	 * information.
	 */

	if (irq == __ipipe_tick_irq) {
		struct pt_regs *tick_regs = &__raw_get_cpu_var(__ipipe_tick_regs);
		tick_regs->flags = regs->flags;
		tick_regs->cs = regs->cs;
		tick_regs->ip = regs->ip;
		tick_regs->bp = regs->bp;
#ifdef CONFIG_X86_64
		tick_regs->ss = regs->ss;
		tick_regs->sp = regs->sp;
#endif
		if (!ipipe_root_domain_p)
			tick_regs->flags &= ~X86_EFLAGS_IF;
	}

	if (!ipipe_root_domain_p ||
	    test_bit(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status)))
		return 0;

#if defined(CONFIG_X86_32) && defined(CONFIG_SMP)
	/*
	 * Prevent a spurious rescheduling from being triggered on
	 * preemptible kernels along the way out through
	 * ret_from_intr.
	 */
	if ((long)regs->orig_ax < 0)
		__set_bit(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status));
#endif	/* CONFIG_SMP */

	return 1;
}

int __ipipe_check_tickdev(const char *devname)
{
#ifdef CONFIG_X86_LOCAL_APIC
	if (!strcmp(devname, "lapic"))
		return __ipipe_check_lapic();
#endif

	return 1;
}

EXPORT_SYMBOL(__ipipe_tick_irq);

EXPORT_SYMBOL_GPL(irq_desc);
struct task_struct *__switch_to(struct task_struct *prev_p,
				struct task_struct *next_p);
EXPORT_SYMBOL_GPL(__switch_to);
EXPORT_SYMBOL_GPL(show_stack);

#ifdef CONFIG_X86_32
EXPORT_PER_CPU_SYMBOL_GPL(init_tss);
#ifdef CONFIG_SMP
EXPORT_PER_CPU_SYMBOL_GPL(cpu_tlbstate);
#endif /* CONFIG_SMP */
#endif /* !CONFIG_X86_32 */

#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
EXPORT_SYMBOL(tasklist_lock);
#endif /* CONFIG_SMP || CONFIG_DEBUG_SPINLOCK */

#ifdef CONFIG_IPIPE_TRACE_MCOUNT
void notrace mcount(void);
EXPORT_SYMBOL(mcount);
#endif /* CONFIG_IPIPE_TRACE_MCOUNT */
