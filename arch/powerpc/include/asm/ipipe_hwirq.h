/* -*- linux-c -*-
 * include/asm-powerpc/ipipe_hwirq.h
 *
 * Copyright (C) 2009 Philippe Gerum.
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

#ifndef _ASM_POWERPC_IPIPE_HWIRQ_H

#ifdef CONFIG_PPC32

#if defined(CONFIG_BOOKE)
#define local_irq_restore_hw_notrace(x)	__asm__ __volatile__("wrtee %0" : : "r" (x) : "memory")
#else
#define local_irq_restore_hw_notrace(x)	mtmsr(x)
#endif

static inline void local_irq_disable_hw_notrace(void)
{
#ifdef CONFIG_BOOKE
	__asm__ __volatile__("wrteei 0": : :"memory");
#else
	unsigned long msr = mfmsr();
	mtmsr(msr & ~MSR_EE);
#endif
}

static inline void local_irq_enable_hw_notrace(void)
{
#ifdef CONFIG_BOOKE
	__asm__ __volatile__("wrteei 1": : :"memory");
#else
	unsigned long msr = mfmsr();
	mtmsr(msr | MSR_EE);
#endif
}

static inline void local_irq_save_ptr_hw(unsigned long *x)
{
	unsigned long msr = mfmsr();
	*x = msr;
#ifdef CONFIG_BOOKE
	__asm__ __volatile__("wrteei 0": : :"memory");
#else
	mtmsr(msr & ~MSR_EE);
#endif
}

#else /* CONFIG_PPC64 */

#include <asm/paca.h>

#ifdef CONFIG_PPC_BOOK3E
static inline void local_irq_disable_hw_notrace(void)
{
	__asm__ __volatile__("wrteei 0": : :"memory");
}

static inline void local_irq_enable_hw_notrace(void)
{
	__asm__ __volatile__("wrteei 1": : :"memory");
}
#else /* !CONFIG_PPC_BOOK3E */
static inline void local_irq_disable_hw_notrace(void)
{
	__mtmsrd(mfmsr() & ~MSR_EE, 1);
}

static inline void local_irq_enable_hw_notrace(void)
{
	__mtmsrd(mfmsr() | MSR_EE, 1);
}
#endif /* !CONFIG_PPC_BOOK3E */

static inline void local_irq_save_ptr_hw(unsigned long *x)
{
	unsigned long msr = mfmsr();
	local_irq_disable_hw_notrace();
	__asm__ __volatile__("": : :"memory");
	*x = msr;
}

#define local_irq_restore_hw_notrace(x)	__mtmsrd(x, 1)

#endif /* CONFIG_PPC64 */

#define local_irq_save_hw_notrace(x)	local_irq_save_ptr_hw(&(x))

#ifdef CONFIG_IPIPE

#include <linux/ipipe_base.h>
#include <linux/ipipe_trace.h>

#ifdef CONFIG_SOFTDISABLE
#error "CONFIG_SOFTDISABLE and CONFIG_IPIPE are mutually exclusive"
#endif

#define irqs_disabled_hw()		((mfmsr() & MSR_EE) == 0)
#define local_save_flags_hw(x)		((x) = mfmsr())
#define arch_irqs_disabled_flags(x)	(((x) & MSR_EE) == 0)

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

#define local_irq_save_hw(x)			       \
	do {					       \
		local_irq_save_ptr_hw(&(x));	       \
		if (!irqs_disabled_flags(x))	       \
			ipipe_trace_begin(0x80000001); \
	} while(0)

static inline void local_irq_restore_hw(unsigned long x)
{
	if (!arch_irqs_disabled_flags(x))
		ipipe_trace_end(0x80000001);

	local_irq_restore_hw_notrace(x);
}

#else /* !CONFIG_IPIPE_TRACE_IRQSOFF */

#define local_irq_disable_hw    local_irq_disable_hw_notrace
#define local_irq_enable_hw     local_irq_enable_hw_notrace
#define local_irq_save_hw       local_irq_save_hw_notrace
#define local_irq_restore_hw    local_irq_restore_hw_notrace

#endif /* CONFIG_IPIPE_TRACE_IRQSOFF */

#define arch_local_irq_disable()			\
	({						\
		unsigned long __x;			\
		ipipe_check_context(ipipe_root_domain);	\
		__x = (!__ipipe_test_and_stall_root()) << MSR_EE_LG;	\
		barrier();						\
		__x;					\
	})

#define arch_local_irq_enable()				\
	do {						\
		barrier();				\
		ipipe_check_context(ipipe_root_domain);	\
		__ipipe_unstall_root();			\
	} while (0)

static inline void arch_local_irq_restore(unsigned long x)
{
	if (!arch_irqs_disabled_flags(x))
		arch_local_irq_enable();
}

#define arch_local_irq_save(x)	arch_local_irq_disable()

#define arch_local_save_flags()						\
	({								\
		unsigned long __x;					\
		__x = (!__ipipe_test_root()) << MSR_EE_LG;		\
		__x;							\
	})

#define arch_irqs_disabled()		__ipipe_test_root()
#define arch_irqs_disabled_flags(x)	(((x) & MSR_EE) == 0)
#define hard_irq_disable()		local_irq_disable_hw()

static inline unsigned long arch_mangle_irq_bits(int virt, unsigned long real)
{
	/*
	 * Merge virtual and real interrupt mask bits into a single
	 * long word. We know MSR_EE will not conflict with 1L<<31.
	 */
	return (real & ~(1L << 31)) | ((long)(virt != 0) << 31);
}

static inline int arch_demangle_irq_bits(unsigned long *x)
{
	int virt = (*x & (1L << 31)) != 0;
	*x &= ~(1L << 31);
	return virt;
}

#else /* !CONFIG_IPIPE */

#define local_irq_save_hw(x)	do { (x) = arch_local_irq_save(); } while (0)
#define local_irq_restore_hw(x)	arch_local_irq_restore(x)
#define local_irq_enable_hw()	arch_local_irq_enable()
#define local_irq_disable_hw()	arch_local_irq_disable()
#define irqs_disabled_hw()	arch_irqs_disabled()

#endif /* !CONFIG_IPIPE */

#endif /* !_ASM_POWERPC_IPIPE_HWIRQ_H */
