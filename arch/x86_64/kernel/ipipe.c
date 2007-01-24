/*   -*- linux-c -*-
 *   linux/arch/x86_64/kernel/ipipe.c
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
 *
 *   Architecture-dependent I-PIPE support for x86_64.
 */

#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <asm/unistd.h>
#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/hw_irq.h>
#include <asm/irq.h>
#include <asm/desc.h>
#include <asm/io.h>
#include <asm/tlbflush.h>
#include <asm/fixmap.h>
#include <asm/bitops.h>
#include <asm/mpspec.h>
#include <asm/io_apic.h>
#include <asm/smp.h>
#include <asm/ipi.h>
#include <asm/mach_apic.h>

struct pt_regs __ipipe_tick_regs[IPIPE_NR_CPUS];

int __ipipe_tick_irq;		/* =0: PIT */

#ifdef CONFIG_SMP

static cpumask_t __ipipe_cpu_sync_map;

static cpumask_t __ipipe_cpu_lock_map;

static IPIPE_DEFINE_SPINLOCK(__ipipe_cpu_barrier);

static atomic_t __ipipe_critical_count = ATOMIC_INIT(0);

static void (*__ipipe_cpu_sync) (void);

u8 __ipipe_apicid_2_cpu[IPIPE_NR_CPUS];

static notrace int __ipipe_boot_cpuid(void)
{
	return 0;
}

int (*__ipipe_logical_cpuid)(void) = &__ipipe_boot_cpuid;

static notrace int __ipipe_hard_cpuid(void)
{
	unsigned long flags;
	int cpu;

	local_irq_save_hw_notrace(flags);
	cpu = __ipipe_apicid_2_cpu[GET_APIC_ID(apic_read(APIC_ID))];
	local_irq_restore_hw_notrace(flags);
	return cpu;
}

#endif /* CONFIG_SMP */

/* ipipe_trigger_irq() -- Push the interrupt at front of the pipeline
   just like if it has been actually received from a hw source. Also
   works for virtual interrupts. */

int fastcall ipipe_trigger_irq(unsigned irq)
{
	struct pt_regs regs;
	unsigned long flags;

	if (irq >= IPIPE_NR_IRQS ||
	    (ipipe_virtual_irq_p(irq) &&
	     !test_bit(irq - IPIPE_VIRQ_BASE, &__ipipe_virtual_irq_map)))
		return -EINVAL;

	local_irq_save_hw(flags);

	regs.orig_rax = irq;	/* Won't be acked */
	regs.cs = __KERNEL_CS;
	regs.eflags = flags;

	__ipipe_handle_irq(&regs);

	local_irq_restore_hw(flags);

	return 1;
}

int ipipe_get_sysinfo(struct ipipe_sysinfo *info)
{
	info->ncpus = num_online_cpus();
	info->cpufreq = ipipe_cpu_freq();
	info->archdep.tmirq = __ipipe_tick_irq;
	info->archdep.tmfreq = ipipe_cpu_freq();

	return 0;
}

int ipipe_tune_timer (unsigned long ns, int flags)

{
	unsigned hz, latch;
	unsigned long x;

	if (flags & IPIPE_RESET_TIMER)
		latch = LATCH;
	else {
		hz = 1000000000 / ns;

		if (hz < HZ)
			return -EINVAL;

		latch = (CLOCK_TICK_RATE + hz/2) / hz;
	}

	x = ipipe_critical_enter(NULL); /* Sync with all CPUs */

	/* Shamelessly lifted from init_IRQ() in i8259.c */
	outb_p(0x34,0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb_p(latch & 0xff,0x40);	/* LSB */
	outb(latch >> 8,0x40);	/* MSB */

	ipipe_critical_exit(x);

	return 0;
}

asmlinkage unsigned int do_IRQ(struct pt_regs *regs);
fastcall void smp_apic_timer_interrupt(struct pt_regs *regs);
fastcall void smp_spurious_interrupt(struct pt_regs *regs);
fastcall void smp_error_interrupt(struct pt_regs *regs);
fastcall void smp_thermal_interrupt(struct pt_regs *regs);
fastcall void smp_reschedule_interrupt(struct pt_regs *regs);
fastcall void smp_invalidate_interrupt(struct pt_regs *regs);
fastcall void smp_call_function_interrupt(struct pt_regs *regs);
fastcall void mce_threshold_interrupt(struct pt_regs *regs);

static int __ipipe_ack_irq(unsigned irq)
{
	irq_desc_t *desc = irq_desc + irq;
	desc->ipipe_ack(irq, desc);
	return 1;
}

void __ipipe_enable_irqdesc(unsigned irq)
{
	irq_desc[irq].status &= ~IRQ_DISABLED;
}

static int __ipipe_noack_apic(unsigned irq)
{
	return 1;
}

int __ipipe_ack_apic(unsigned irq)
{
	__ack_APIC_irq();
	return 1;
}

static void __ipipe_null_handler(unsigned irq, void *cookie)
{
}

/* __ipipe_enable_pipeline() -- We are running on the boot CPU, hw
   interrupts are off, and secondary CPUs are still lost in space. */

void __init __ipipe_enable_pipeline(void)
{
	unsigned irq;

	/* Map the APIC system vectors. */

	ipipe_virtualize_irq(ipipe_root_domain,
			     LOCAL_TIMER_VECTOR - FIRST_EXTERNAL_VECTOR,
			     (ipipe_irq_handler_t)&smp_apic_timer_interrupt,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

	ipipe_virtualize_irq(ipipe_root_domain,
			     SPURIOUS_APIC_VECTOR - FIRST_EXTERNAL_VECTOR,
			     (ipipe_irq_handler_t)&smp_spurious_interrupt,
			     NULL,
			     &__ipipe_noack_apic,
			     IPIPE_STDROOT_MASK);

	ipipe_virtualize_irq(ipipe_root_domain,
			     ERROR_APIC_VECTOR - FIRST_EXTERNAL_VECTOR,
			     (ipipe_irq_handler_t)&smp_error_interrupt,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

	ipipe_virtualize_irq(ipipe_root_domain,
			     IPIPE_SERVICE_VECTOR0 - FIRST_EXTERNAL_VECTOR,
			     &__ipipe_null_handler,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

	ipipe_virtualize_irq(ipipe_root_domain,
			     IPIPE_SERVICE_VECTOR1 - FIRST_EXTERNAL_VECTOR,
			     &__ipipe_null_handler,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

	ipipe_virtualize_irq(ipipe_root_domain,
			     IPIPE_SERVICE_VECTOR2 - FIRST_EXTERNAL_VECTOR,
			     &__ipipe_null_handler,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

	ipipe_virtualize_irq(ipipe_root_domain,
			     IPIPE_SERVICE_VECTOR3 - FIRST_EXTERNAL_VECTOR,
			     &__ipipe_null_handler,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

	ipipe_virtualize_irq(ipipe_root_domain,
			     THRESHOLD_APIC_VECTOR - FIRST_EXTERNAL_VECTOR,
			     (ipipe_irq_handler_t)&mce_threshold_interrupt,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

	ipipe_virtualize_irq(ipipe_root_domain,
			     THERMAL_APIC_VECTOR - FIRST_EXTERNAL_VECTOR,
			     (ipipe_irq_handler_t)&smp_thermal_interrupt,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

#ifdef CONFIG_SMP
	ipipe_virtualize_irq(ipipe_root_domain,
			     RESCHEDULE_VECTOR - FIRST_EXTERNAL_VECTOR,
			     (ipipe_irq_handler_t)&smp_reschedule_interrupt,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

	for (irq = INVALIDATE_TLB_VECTOR_START - FIRST_EXTERNAL_VECTOR;
	     irq < NUM_INVALIDATE_TLB_VECTORS; ++irq)
	    ipipe_virtualize_irq(ipipe_root_domain,
				 irq,
				 (ipipe_irq_handler_t)&smp_invalidate_interrupt,
				 NULL,
				 &__ipipe_ack_apic,
				 IPIPE_STDROOT_MASK);

	ipipe_virtualize_irq(ipipe_root_domain,
			     CALL_FUNCTION_VECTOR - FIRST_EXTERNAL_VECTOR,
			     (ipipe_irq_handler_t)&smp_call_function_interrupt,
			     NULL,
			     &__ipipe_ack_apic,
			     IPIPE_STDROOT_MASK);

	/* Some guest O/S may run tasks over non-Linux stacks, so we
	 * cannot rely on the regular definition of smp_processor_id()
	 * on x86 to fetch the logical cpu id. We fix this by using
	 * our own private physical apicid -> logicial cpuid mapping
	 * as soon as the pipeline is enabled, so that
	 * ipipe_processor_id() always do the right thing, regardless
	 * of the current stack setup. Also note that the pipeline is
	 * enabled after the APIC space has been mapped in
	 * trap_init(), so it's safe to use it. */

	__ipipe_logical_cpuid = &__ipipe_hard_cpuid;
#endif	/* CONFIG_SMP */

	/* Finally, virtualize the remaining ISA and IO-APIC
	 * interrupts. Interrupts which have already been virtualized
	 * will just beget a silent -EPERM error since
	 * IPIPE_SYSTEM_MASK has been passed for them, that's ok. */

	for (irq = 0; irq < NR_IRQS; irq++)
		/* Fails for IPIPE_CRITICAL_IPI but that's ok. */
		ipipe_virtualize_irq(ipipe_root_domain,
				     irq,
				     (ipipe_irq_handler_t)&do_IRQ, /* Via thunk. */
				     NULL,
				     &__ipipe_ack_irq,
				     IPIPE_STDROOT_MASK);

	/* Eventually allow these vectors to be reprogrammed. */
	ipipe_root_domain->irqs[IPIPE_SERVICE_IPI0].control &= ~IPIPE_SYSTEM_MASK;
	ipipe_root_domain->irqs[IPIPE_SERVICE_IPI1].control &= ~IPIPE_SYSTEM_MASK;
	ipipe_root_domain->irqs[IPIPE_SERVICE_IPI2].control &= ~IPIPE_SYSTEM_MASK;
	ipipe_root_domain->irqs[IPIPE_SERVICE_IPI3].control &= ~IPIPE_SYSTEM_MASK;
}

#ifdef CONFIG_SMP

cpumask_t __ipipe_set_irq_affinity (unsigned irq, cpumask_t cpumask)

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

int fastcall __ipipe_send_ipi (unsigned ipi, cpumask_t cpumask)

{
	unsigned long flags;
	ipipe_declare_cpuid;
	int self;

	if (ipi != IPIPE_SERVICE_IPI0 &&
	    ipi != IPIPE_SERVICE_IPI1 &&
	    ipi != IPIPE_SERVICE_IPI2 &&
	    ipi != IPIPE_SERVICE_IPI3)
		return -EINVAL;

	ipipe_lock_cpu(flags);

	self = cpu_isset(cpuid,cpumask);
	cpu_clear(cpuid,cpumask);

	if (!cpus_empty(cpumask))
		send_IPI_mask(cpumask,ipi + FIRST_EXTERNAL_VECTOR);

	if (self)
		ipipe_trigger_irq(ipi);

	ipipe_unlock_cpu(flags);

	return 0;
}

/* Always called with hw interrupts off. */

void __ipipe_do_critical_sync(unsigned irq, void *cookie)
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

/* ipipe_critical_exit() -- Release the superlock. */

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

static inline void __fixup_if(struct pt_regs *regs)
{
	ipipe_declare_cpuid;
	unsigned long flags;

	ipipe_get_cpu(flags);

	if (per_cpu(ipipe_percpu_domain, cpuid) == ipipe_root_domain) {
		/* Have the saved hw state look like the domain stall bit, so
		   that __ipipe_unstall_iret_root() restores the proper
		   pipeline state for the root stage upon exit. */

		if (test_bit
		    (IPIPE_STALL_FLAG,
		     &ipipe_root_domain->cpudata[cpuid].status))
			regs->eflags &= ~X86_EFLAGS_IF;
		else
			regs->eflags |= X86_EFLAGS_IF;
	}

	ipipe_put_cpu(flags);
}

/*  Check the stall bit of the root domain to make sure the existing
    preemption opportunity upon in-kernel resumption could be
    exploited. In case a rescheduling could take place, the root stage
    is stalled before the hw interrupts are re-enabled. This routine
    must be called with hw interrupts off. */

asmlinkage int __ipipe_kpreempt_root(void)
{
	ipipe_declare_cpuid;
	unsigned long flags;

	ipipe_get_cpu(flags);

	if (test_bit
	    (IPIPE_STALL_FLAG, &ipipe_root_domain->cpudata[cpuid].status)) {
		ipipe_put_cpu(flags);
		return 0;	/* Root stage is stalled: rescheduling denied. */
	}

	__ipipe_stall_root();
	local_irq_enable_hw_notrace();

	return 1;		/* Ok, may reschedule now. */
}

asmlinkage void __ipipe_unstall_iret_root(struct pt_regs *regs)
{
	ipipe_declare_cpuid;

	/* Emulate IRET's handling of the interrupt flag. */

	local_irq_disable_hw();

	ipipe_load_cpuid();

	/* Restore the software state as it used to be on kernel
	   entry. CAUTION: NMIs must *not* return through this
	   emulation. */

	if (!(regs->eflags & X86_EFLAGS_IF)) {
		if (!__test_and_set_bit(IPIPE_STALL_FLAG,
					&ipipe_root_domain->cpudata[cpuid].status))
			trace_hardirqs_off();
		regs->eflags |= X86_EFLAGS_IF;
	} else {
		if (test_bit(IPIPE_STALL_FLAG,
			     &ipipe_root_domain->cpudata[cpuid].status)) {
			trace_hardirqs_on();
			__clear_bit(IPIPE_STALL_FLAG,
				    &ipipe_root_domain->cpudata[cpuid].status);
		}

		/* Only sync virtual IRQs here, so that we don't recurse
		   indefinitely in case of an external interrupt flood. */

		if ((ipipe_root_domain->cpudata[cpuid].
		     irq_pending_hi & IPIPE_IRQMASK_VIRT) != 0)
			__ipipe_sync_pipeline(IPIPE_IRQMASK_VIRT);
	}
#ifdef CONFIG_IPIPE_TRACE_IRQSOFF
	ipipe_trace_end(0x8000000D);
#endif /* CONFIG_IPIPE_TRACE_IRQSOFF */
}

asmlinkage int __ipipe_syscall_root(struct pt_regs *regs)
{
	ipipe_declare_cpuid;
	unsigned long flags;

	__fixup_if(regs);

	/* This routine either returns:
	    0 -- if the syscall is to be passed to Linux;
	   >0 -- if the syscall should not be passed to Linux, and no
	   tail work should be performed;
	   <0 -- if the syscall should not be passed to Linux but the
	   tail work has to be performed (for handling signals etc). */

	if (__ipipe_syscall_watched_p(current, regs->orig_rax) &&
	    __ipipe_event_monitored_p(IPIPE_EVENT_SYSCALL) &&
	    __ipipe_dispatch_event(IPIPE_EVENT_SYSCALL,regs) > 0) {
		/* We might enter here over a non-root domain and exit
		 * over the root one as a result of the syscall
		 * (i.e. by recycling the register set of the current
		 * context across the migration), so we need to fixup
		 * the interrupt flag upon return too, so that
		 * __ipipe_unstall_iret_root() resets the correct
		 * stall bit on exit. */
		__fixup_if(regs);

		if (ipipe_current_domain == ipipe_root_domain && !in_atomic()) {
			/* Sync pending VIRQs before _TIF_NEED_RESCHED
			 * is tested. */
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

static fastcall void do_machine_check_vector(struct pt_regs *regs, long error_code)
{
#ifdef CONFIG_X86_MCE
	void do_machine_check(struct pt_regs * regs, long error_code);
	do_machine_check(regs,error_code);
#endif /* CONFIG_X86_MCE */
}

fastcall void do_divide_error(struct pt_regs *regs, long error_code);
fastcall void do_overflow(struct pt_regs *regs, long error_code);
fastcall void do_bounds(struct pt_regs *regs, long error_code);
fastcall void do_invalid_op(struct pt_regs *regs, long error_code);
fastcall void do_coprocessor_segment_overrun(struct pt_regs *regs, long error_code);
fastcall void do_invalid_TSS(struct pt_regs *regs, long error_code);
fastcall void do_segment_not_present(struct pt_regs *regs, long error_code);
fastcall void do_stack_segment(struct pt_regs *regs, long error_code);
fastcall void do_general_protection(struct pt_regs *regs, long error_code);
fastcall void do_page_fault(struct pt_regs *regs, long error_code);
fastcall void do_spurious_interrupt_bug(struct pt_regs *regs, long error_code);
fastcall void do_coprocessor_error(struct pt_regs *regs, long error_code);
fastcall void do_alignment_check(struct pt_regs *regs, long error_code);
fastcall void do_simd_coprocessor_error(struct pt_regs *regs, long error_code);

/* Work around genksyms's issue with over-qualification in decls. */

typedef fastcall void __ipipe_exhandler(struct pt_regs *, long);

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
	[ex_do_page_fault] = &do_page_fault,
	[ex_do_spurious_interrupt_bug] = &do_spurious_interrupt_bug,
	[ex_do_coprocessor_error] = &do_coprocessor_error,
	[ex_do_alignment_check] = &do_alignment_check,
	[ex_machine_check_vector] = &do_machine_check_vector,
	[ex_do_simd_coprocessor_error] = &do_simd_coprocessor_error,
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
	[ex_device_not_available] = -1,
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
};
#endif /* CONFIG_KGDB */

fastcall int __ipipe_handle_exception(struct pt_regs *regs, long error_code, int vector)
{
	unsigned long flags;

	local_save_flags(flags);

	/* Track the hw interrupt state before calling the Linux
	 * exception handler, replicating it into the virtual mask. */

	if (irqs_disabled_hw())
		local_irq_disable();

#ifdef CONFIG_KGDB
	/* catch exception KGDB is interested in over non-root domains */
	if (ipipe_current_domain != ipipe_root_domain &&
	    __ipipe_xlate_signo[vector] >= 0 &&
	    !kgdb_handle_exception(vector, __ipipe_xlate_signo[vector], error_code, regs)) {
		local_irq_restore(flags);
		return 1;
	}
#endif /* CONFIG_KGDB */

	if (!ipipe_trap_notify(vector, regs)) {
		__ipipe_exptr handler = __ipipe_std_extable[vector];
		handler(regs,error_code);
		local_irq_restore(flags);
		__fixup_if(regs);
		return 0;
	}

	local_irq_restore(flags);

	return 1;
}

fastcall int __ipipe_divert_exception(struct pt_regs *regs, int vector)
{
#ifdef CONFIG_KGDB
	/* catch int1 and int3 over non-root domains */
	if ((ipipe_current_domain != ipipe_root_domain) &&
	    (vector != ex_device_not_available)) {
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

int __ipipe_handle_irq(struct pt_regs *regs)
{
	struct ipipe_domain *this_domain, *next_domain;
	unsigned vector = ~regs->orig_rax, irq;
	struct list_head *head, *pos;
	ipipe_declare_cpuid;
	int m_ack;

	if ((long)regs->orig_rax < 0) {
		if (vector >= FIRST_SYSTEM_VECTOR)
			irq = IPIPE_FIRST_APIC_IRQ + vector - FIRST_SYSTEM_VECTOR;
		else
			irq = __get_cpu_var(vector_irq)[vector];
		m_ack = 0;
	} else { /* This is a self-triggered one. */
		irq = vector;
		m_ack = 1;
	}

	ipipe_load_cpuid();

	head = __ipipe_pipeline.next;
	next_domain = list_entry(head, struct ipipe_domain, p_link);
	if (likely(test_bit(IPIPE_WIRED_FLAG, &next_domain->irqs[irq].control))) {
		if (!m_ack && next_domain->irqs[irq].acknowledge != NULL)
			next_domain->irqs[irq].acknowledge(irq);
		if (likely(__ipipe_dispatch_wired(next_domain, irq))) {
			ipipe_load_cpuid();
			goto finalize;
		} else
			goto finalize_nosync;
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

			if (!m_ack && next_domain->irqs[irq].acknowledge != NULL)
				m_ack = next_domain->irqs[irq].acknowledge(irq);
		}

		/*
		 * If the domain does not want the IRQ to be passed
		 * down the interrupt pipe, exit the loop now.
		 */

		if (!test_bit(IPIPE_PASS_FLAG, &next_domain->irqs[irq].control))
			break;

		pos = next_domain->p_link.next;
	}

	if (irq == __ipipe_tick_irq &&
	    __ipipe_pipeline_head_p(ipipe_root_domain) &&
	    ipipe_root_domain->cpudata[cpuid].irq_counters[irq].pending_hits > 1)
		/*
		 * Emulate a loss of clock ticks if Linux is owning
		 * the time source. The drift will be compensated by
		 * the timer support code.
		 */
		ipipe_root_domain->cpudata[cpuid].irq_counters[irq].pending_hits = 1;

finalize:

	if (irq == __ipipe_tick_irq) {
		__ipipe_tick_regs[cpuid].eflags = regs->eflags;
		__ipipe_tick_regs[cpuid].rsp = regs->rsp;
		__ipipe_tick_regs[cpuid].rip = regs->rip;
		__ipipe_tick_regs[cpuid].cs = regs->cs;
	}

	/*
	 * Now walk the pipeline, yielding control to the highest
	 * priority domain that has pending interrupt(s) or
	 * immediately to the current domain if the interrupt has been
	 * marked as 'sticky'. This search does not go beyond the
	 * current domain in the pipeline.
	 */

	__ipipe_walk_pipeline(head, cpuid);

	ipipe_load_cpuid();

finalize_nosync:

	if (per_cpu(ipipe_percpu_domain, cpuid) != ipipe_root_domain ||
	    test_bit(IPIPE_STALL_FLAG, &ipipe_root_domain->cpudata[cpuid].status))
		return 0;

#ifdef CONFIG_SMP
	/*
	 * Prevent a spurious rescheduling from being triggered on
	 * preemptible kernels along the way out through
	 * ret_from_intr.
	 */
	if ((long)regs->orig_rax < 0)
		__set_bit(IPIPE_STALL_FLAG, &ipipe_root_domain->cpudata[cpuid].status);
#endif	/* CONFIG_SMP */

	return 1;
}

EXPORT_SYMBOL(__ipipe_tick_irq);
EXPORT_SYMBOL(ipipe_critical_enter);
EXPORT_SYMBOL(ipipe_critical_exit);
EXPORT_SYMBOL(ipipe_trigger_irq);
EXPORT_SYMBOL(ipipe_get_sysinfo);
EXPORT_SYMBOL(ipipe_tune_timer);

EXPORT_SYMBOL_GPL(irq_desc);
struct task_struct *__switch_to(struct task_struct *prev_p,
				struct task_struct *next_p);
EXPORT_SYMBOL_GPL(__switch_to);
EXPORT_SYMBOL_GPL(show_stack);
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
EXPORT_SYMBOL(tasklist_lock);
#endif /* CONFIG_SMP || CONFIG_DEBUG_SPINLOCK */
#ifdef CONFIG_SMP
EXPORT_SYMBOL(__ipipe_logical_cpuid);
#endif /* CONFIG_SMP */

#ifdef CONFIG_IPIPE_TRACE_MCOUNT
void notrace mcount(void);
EXPORT_SYMBOL(mcount);
#endif /* CONFIG_IPIPE_TRACE_MCOUNT */
