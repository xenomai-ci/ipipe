/*
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 */
#ifndef _ASM_POWERPC_HW_IRQ_H
#define _ASM_POWERPC_HW_IRQ_H

#ifdef __KERNEL__

#include <linux/errno.h>
#include <linux/compiler.h>
#include <asm/ptrace.h>
#include <asm/processor.h>

extern void timer_interrupt(struct pt_regs *);

#ifdef CONFIG_PPC64

#ifdef CONFIG_SOFTDISABLE
#include <asm/paca.h>

static inline unsigned long local_get_flags(void)
{
	unsigned long flags;

	__asm__ __volatile__("lbz %0,%1(13)"
	: "=r" (flags)
	: "i" (offsetof(struct paca_struct, soft_enabled)));

	return flags;
}

static inline unsigned long raw_local_irq_disable(void)
{
	unsigned long flags, zero;

	__asm__ __volatile__("li %1,0; lbz %0,%2(13); stb %1,%2(13)"
	: "=r" (flags), "=&r" (zero)
	: "i" (offsetof(struct paca_struct, soft_enabled))
	: "memory");

	return flags;
}

extern void raw_local_irq_restore(unsigned long);
extern void iseries_handle_interrupts(void);

#define raw_local_irq_enable()		raw_local_irq_restore(1)
#define raw_local_save_flags(flags)	((flags) = local_get_flags())
#define raw_local_irq_save(flags)	((flags) = raw_local_irq_disable())

#define raw_irqs_disabled()		(local_get_flags() == 0)
#define raw_irqs_disabled_flags(flags)	((flags) == 0)

#define __hard_irq_enable()	__mtmsrd(mfmsr() | MSR_EE, 1)
#define __hard_irq_disable()	__mtmsrd(mfmsr() & ~MSR_EE, 1)

#define  hard_irq_disable()			\
	do {					\
		__hard_irq_disable();		\
		get_paca()->soft_enabled = 0;	\
		get_paca()->hard_enabled = 0;	\
	} while(0)

static inline int irqs_disabled_flags(unsigned long flags)
{
	return flags == 0;
}

#define local_irq_save_hw(x)	raw_local_irq_save(x)
#define local_irq_restore_hw(x)	raw_local_irq_restore(x)
#define local_irq_enable_hw()	raw_local_irq_enable()
#define local_irq_disable_hw()	raw_local_irq_disable()
#define irqs_disabled_hw()	raw_irqs_disabled()

#else /* !CONFIG_SOFTDISABLE */

#ifdef CONFIG_IPIPE

#include <linux/ipipe_base.h>
#include <linux/ipipe_trace.h>

#define raw_local_save_flags(x)		do {			\
		(x) = (!__ipipe_test_root()) << MSR_EE_LG;	\
		__asm__ __volatile__("": : :"memory");		\
	} while(0)

#define raw_local_irq_restore(x)	do {			\
		__asm__ __volatile__("": : :"memory");		\
		__ipipe_restore_root(!((x) & MSR_EE));		\
	} while(0)

static inline void raw_local_irq_save_ptr(unsigned long *x)
{
	*x = (!__ipipe_test_and_stall_root()) << MSR_EE_LG;
	barrier();
}

#define raw_local_irq_save(x)			\
do {						\
	ipipe_check_context(ipipe_root_domain);	\
	raw_local_irq_save_ptr(&(x));		\
} while(0)

#define hard_irq_enable()	do {				\
		barrier();					\
		__ipipe_unstall_root();				\
	} while(0)

#define hard_irq_disable()	do {				\
		ipipe_check_context(ipipe_root_domain);		\
		__ipipe_stall_root();				\
		barrier();					\
	} while(0)

#define raw_local_irq_disable()	hard_irq_disable()
#define raw_local_irq_enable()	hard_irq_enable()
#define raw_irqs_disabled()	(__ipipe_test_root() != 0)

static inline int raw_irqs_disabled_flags(unsigned long x)
{
	return !(x & MSR_EE);
}

static inline unsigned long raw_mangle_irq_bits(int virt, unsigned long real)
{
	/* Merge virtual and real interrupt mask bits into a single
	   64bit word. We know MSR_EE will not conflict with 1L<<31. */
	return (real & ~(1L << 31)) | ((long)virt << 31);
}

static inline int raw_demangle_irq_bits(unsigned long *x)
{
	int virt = (*x & (1L << 31)) != 0;
	*x &= ~(1L << 31);
	return virt;
}

#define local_irq_disable_hw_notrace()		__mtmsrd(mfmsr() & ~MSR_EE, 1)
#define local_irq_enable_hw_notrace()		__mtmsrd(mfmsr() | MSR_EE, 1)
#define local_irq_save_hw_notrace(x)		({ (x) = __local_irq_save_hw(); })
#define local_irq_restore_hw_notrace(x)		__mtmsrd(x, 1)

static inline unsigned long __local_irq_save_hw(void)
{
	unsigned long msr = mfmsr();
	local_irq_disable_hw_notrace();
	__asm__ __volatile__("": : :"memory");
	return msr;
}
	
#define local_save_flags_hw(x)		((x) = mfmsr())
#define irqs_disabled_hw()		((mfmsr() & MSR_EE) == 0)

#ifdef CONFIG_IPIPE_TRACE_IRQSOFF
#define local_irq_disable_hw() do {			\
		if (!irqs_disabled_hw()) {		\
			local_irq_disable_hw_notrace();	\
			ipipe_trace_begin(0x80000000);	\
		}					\
	} while (0)
#define local_irq_enable_hw() do {			\
		if (irqs_disabled_hw()) {		\
			ipipe_trace_end(0x80000000);	\
			local_irq_enable_hw_notrace();	\
		}					\
	} while (0)
#define local_irq_save_hw(x) do {			\
		local_save_flags_hw(x);			\
		if ((x) & MSR_EE) {			\
			local_irq_disable_hw_notrace();	\
			ipipe_trace_begin(0x80000001);	\
		}					\
	} while (0)
#define local_irq_restore_hw(x) do {			\
		if ((x) & MSR_EE)			\
			ipipe_trace_end(0x80000001);	\
		local_irq_restore_hw_notrace(x);	\
	} while (0)
#else /* !CONFIG_IPIPE_TRACE_IRQSOFF */
#define local_irq_save_hw(x)	local_irq_save_hw_notrace(x)
#define local_irq_restore_hw(x)	local_irq_restore_hw_notrace(x)
#define local_irq_enable_hw()	local_irq_enable_hw_notrace()
#define local_irq_disable_hw()	local_irq_disable_hw_notrace()
#endif /* CONFIG_IPIPE_TRACE_IRQSOFF */

#else /* !CONFIG_IPIPE */

#define hard_irq_enable()	__mtmsrd(mfmsr() | MSR_EE, 1)
#define hard_irq_disable()	__mtmsrd(mfmsr() & ~MSR_EE, 1)

#define raw_local_save_flags(x)		((x) = mfmsr())
#define raw_local_irq_restore(x) 	__mtmsrd(x, 1)
#define raw_irqs_disabled()		((mfmsr() & MSR_EE) == 0)

#define local_irq_save_hw(x)	raw_local_irq_save(x)
#define local_irq_restore_hw(x)	raw_local_irq_restore(x)
#define local_irq_enable_hw()	hard_irq_enable()
#define local_irq_disable_hw()	hard_irq_disable()
#define irqs_disabled_hw()	raw_irqs_disabled()

#endif /* !CONFIG_IPIPE */

#endif /* !CONFIG_SOFTDISABLE */

#else /* !CONFIG_PPC64 */

static inline unsigned long raw_mangle_irq_bits(int virt, unsigned long real)
{
	/* Merge virtual and real interrupt mask bits into a single
	   32bit word. */
	return (real & ~(1 << 31)) | ((virt != 0) << 31);
}

static inline int raw_demangle_irq_bits(unsigned long *x)
{
	int virt = (*x & (1 << 31)) != 0;
	*x &= ~(1L << 31);
	return virt;
}

#define local_save_flags_hw(x)			((x) = mfmsr())
#define local_test_iflag_hw(x)			((x) & MSR_EE)
#define irqs_disabled_hw()			((mfmsr() & MSR_EE) == 0)
#define local_irq_save_hw_notrace(x)		local_irq_save_ptr_hw(&(x))
#define raw_irqs_disabled_flags(x)		(!local_test_iflag_hw(x))

#if defined(CONFIG_BOOKE)
#define local_irq_restore_hw_notrace(x)	\
	__asm__ __volatile__("wrtee %0" : : "r" (x) : "memory")
#else
#define SET_MSR_EE(x)	mtmsr(x)
#define local_irq_restore_hw_notrace(x)	mtmsr(x)
#endif

static inline void local_irq_disable_hw_notrace(void)
{
#ifdef CONFIG_BOOKE
	__asm__ __volatile__("wrteei 0": : :"memory");
#else
	unsigned long msr;
	__asm__ __volatile__("": : :"memory");
	msr = mfmsr();
	SET_MSR_EE(msr & ~MSR_EE);
#endif
}

static inline void local_irq_enable_hw_notrace(void)
{
#ifdef CONFIG_BOOKE
	__asm__ __volatile__("wrteei 1": : :"memory");
#else
	unsigned long msr;
	__asm__ __volatile__("": : :"memory");
	msr = mfmsr();
	SET_MSR_EE(msr | MSR_EE);
#endif
}

static inline void local_irq_save_ptr_hw(unsigned long *x)
{
	unsigned long msr;
	msr = mfmsr();
	*x = msr;
#ifdef CONFIG_BOOKE
	__asm__ __volatile__("wrteei 0": : :"memory");
#else
	SET_MSR_EE(msr & ~MSR_EE);
#endif
	__asm__ __volatile__("": : :"memory");
}

#ifdef CONFIG_IPIPE

#include <linux/ipipe_base.h>
#include <linux/ipipe_trace.h>

#ifdef CONFIG_IPIPE_TRACE_IRQSOFF

static inline void local_irq_disable_hw(void)
{
	if (!irqs_disabled_hw()) {
		local_irq_disable_hw_notrace();
		ipipe_trace_begin(0x80000000);
	}
}

static inline void local_irq_enable_hw(void)
{
	if (irqs_disabled_hw()) {
		ipipe_trace_end(0x80000000);
		local_irq_enable_hw_notrace();
	}
}

#define local_irq_save_hw(x) \
do {						       \
	local_irq_save_ptr_hw(&(x));		       \
	if (local_test_iflag_hw(x))		       \
		ipipe_trace_begin(0x80000001);	       \
} while(0)

static inline void local_irq_restore_hw(unsigned long x)
{
	if (local_test_iflag_hw(x))
		ipipe_trace_end(0x80000001);

	local_irq_restore_hw_notrace(x);
}

#else /* !CONFIG_IPIPE_TRACE_IRQSOFF */

#define local_irq_disable_hw    local_irq_disable_hw_notrace
#define local_irq_enable_hw     local_irq_enable_hw_notrace
#define local_irq_save_hw       local_irq_save_hw_notrace
#define local_irq_restore_hw    local_irq_restore_hw_notrace

#endif /* CONFIG_IPIPE_TRACE_IRQSOFF */

static inline void local_irq_disable(void)
{
	ipipe_check_context(ipipe_root_domain);
	__ipipe_stall_root();
	barrier();
}

static inline void local_irq_enable(void)
{
	barrier();
	__ipipe_unstall_root();
}

static inline void local_irq_save_ptr(unsigned long *x)
{
	*x = (!__ipipe_test_and_stall_root()) << MSR_EE_LG;
	barrier();
}

static inline void local_irq_restore(unsigned long x)
{
	barrier();
	__ipipe_restore_root(!(x & MSR_EE));
}

#define local_save_flags(x)			\
do {						\
	(x) = (!__ipipe_test_root()) << MSR_EE_LG;	\
	barrier();				\
} while(0)

#define local_irq_save(x)			\
do {						\
	ipipe_check_context(ipipe_root_domain);	\
	local_irq_save_ptr(&(x));		\
} while(0)

#define irqs_disabled()		__ipipe_test_root()

#else /* !CONFIG_IPIPE */

#define local_irq_disable_hw    	local_irq_disable_hw_notrace
#define local_irq_enable_hw     	local_irq_enable_hw_notrace
#define local_irq_save_hw       	local_irq_save_hw_notrace
#define local_irq_restore_hw    	local_irq_restore_hw_notrace
#define local_irq_restore(x)		local_irq_restore_hw(x)
#define local_irq_disable()		local_irq_disable_hw()
#define local_irq_enable()		local_irq_enable_hw()
#define local_irq_save_ptr(x)		local_irq_save_ptr_hw(x)
#define irqs_disabled()			irqs_disabled_hw()
#define local_save_flags(x)		local_save_flags_hw(x)
#define local_irq_save(x)  		local_irq_save_hw(x)

#endif /* !CONFIG_IPIPE */

#define hard_irq_enable()	local_irq_enable()
#define hard_irq_disable()	local_irq_disable()

static inline int irqs_disabled_flags(unsigned long flags)
{
	return (flags & MSR_EE) == 0;
}

#endif /* CONFIG_PPC64 */

/*
 * interrupt-retrigger: should we handle this via lost interrupts and IPIs
 * or should we not care like we do now ? --BenH.
 */
struct hw_interrupt_type;

#endif	/* __KERNEL__ */
#endif	/* _ASM_POWERPC_HW_IRQ_H */
