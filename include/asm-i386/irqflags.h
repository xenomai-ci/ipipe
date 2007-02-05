/*
 * include/asm-i386/irqflags.h
 *
 * IRQ flags handling
 *
 * This file gets included from lowlevel asm headers too, to provide
 * wrapped versions of the local_irq_*() APIs, based on the
 * raw_local_irq_*() functions from the lowlevel headers.
 */
#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#ifndef __ASSEMBLY__

#include <linux/ipipe_trace.h>

void __ipipe_stall_root(void);

void __ipipe_unstall_root(void);

unsigned long __ipipe_test_root(void);

unsigned long __ipipe_test_and_stall_root(void);

void fastcall __ipipe_restore_root(unsigned long flags);

static inline unsigned long __raw_local_save_flags(void)
{
	unsigned long flags;

#ifdef CONFIG_IPIPE
	flags = (!__ipipe_test_root()) << 9;
#else
	__asm__ __volatile__(
		"pushfl ; popl %0"
		: "=g" (flags)
		: /* no input */
	);
#endif

	return flags;
}

static inline void raw_local_irq_restore(unsigned long flags)
{
#ifdef CONFIG_IPIPE
	__ipipe_restore_root(!(flags & 0x200));
#else
	__asm__ __volatile__(
		"pushl %0 ; popfl"
		: /* no output */
		:"g" (flags)
		:"memory", "cc"
	);
#endif
}

static inline void raw_local_irq_disable(void)
{
#ifdef CONFIG_IPIPE
	__ipipe_stall_root();
#else
	__asm__ __volatile__("cli" : : : "memory");
#endif
}

static inline void raw_local_irq_enable(void)
{
#ifdef CONFIG_IPIPE
	__ipipe_unstall_root();
#else
	__asm__ __volatile__("sti" : : : "memory");
#endif
}

/*
 * Used in the idle loop; sti takes one instruction cycle
 * to complete:
 */
static inline void raw_safe_halt(void)
{
#ifdef CONFIG_IPIPE
	__ipipe_unstall_root();
#ifdef CONFIG_IPIPE_TRACE_IRQSOFF
	ipipe_trace_end(0x8000000E);
#endif
#endif
	__asm__ __volatile__("sti; hlt" : : : "memory");
}

/*
 * Used when interrupts are already enabled or to
 * shutdown the processor:
 */
static inline void halt(void)
{
	__asm__ __volatile__("hlt": : :"memory");
}

/*
 * For spinlocks, etc:
 */
static inline unsigned long __raw_local_irq_save(void)
{
	unsigned long flags = __raw_local_save_flags();

	raw_local_irq_disable();

	return flags;
}

#else

#ifdef CONFIG_IPIPE
#define DISABLE_INTERRUPTS(clobbers)		call __ipipe_stall_root	;  sti
#define ENABLE_INTERRUPTS(clobbers)		call __ipipe_unstall_root
#define ENABLE_INTERRUPTS_HW_COND		sti
#define DISABLE_INTERRUPTS_HW(clobbers)    	cli
#define ENABLE_INTERRUPTS_HW(clobbers)	sti
#else /* !CONFIG_IPIPE */
#define DISABLE_INTERRUPTS(clobbers)		cli
#define ENABLE_INTERRUPTS(clobbers)		sti
#define ENABLE_INTERRUPTS_HW_COND
#define DISABLE_INTERRUPTS_HW(clobbers)    	DISABLE_INTERRUPTS(clobbers)
#define ENABLE_INTERRUPTS_HW(clobbers)	ENABLE_INTERRUPTS(clobbers)
#endif /* !CONFIG_IPIPE */
#define ENABLE_INTERRUPTS_SYSEXIT	sti; sysexit
#define INTERRUPT_RETURN		iret
#define GET_CR0_INTO_EAX		movl %cr0, %eax
#endif /* __ASSEMBLY__ */
#endif /* CONFIG_PARAVIRT */

#ifndef __ASSEMBLY__
#define raw_local_save_flags(flags) \
		do { (flags) = __raw_local_save_flags(); } while (0)

#define raw_local_irq_save(flags) \
		do { (flags) = __raw_local_irq_save(); } while (0)

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & (1 << 9));
}

static inline int raw_irqs_disabled(void)
{
	unsigned long flags = __raw_local_save_flags();

	return raw_irqs_disabled_flags(flags);
}

#define local_irq_disable_hw_notrace() \
	__asm__ __volatile__("cli": : :"memory")
#define local_irq_enable_hw_notrace() \
	__asm__ __volatile__("sti": : :"memory")
#define local_irq_save_hw_notrace(x) \
	__asm__ __volatile__("pushfl ; popl %0 ; cli":"=g" (x): /* no input */ :"memory")
#define local_irq_restore_hw_notrace(x) \
	__asm__ __volatile__("pushl %0 ; popfl": /* no output */ :"g" (x):"memory", "cc")

#define local_save_flags_hw(x)	__asm__ __volatile__("pushfl ; popl %0":"=g" (x): /* no input */)
#define irqs_disabled_hw()		\
    ({					\
	unsigned long x;		\
	local_save_flags_hw(x);		\
	!((x) & (1 << 9));		\
    })

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
		if ((x) & (1 << 9)) {			\
			local_irq_disable_hw_notrace();	\
			ipipe_trace_begin(0x80000001);	\
		}					\
	} while (0)
#define local_irq_restore_hw(x) do {			\
		if ((x) & (1 << 9))			\
			ipipe_trace_end(0x80000001);	\
		local_irq_restore_hw_notrace(x);	\
	} while (0)
#else /* !CONFIG_IPIPE_TRACE_IRQSOFF */
#define local_irq_save_hw(x)		local_irq_save_hw_notrace(x)
#define local_irq_restore_hw(x)	local_irq_restore_hw_notrace(x)
#define local_irq_enable_hw()		local_irq_enable_hw_notrace()
#define local_irq_disable_hw()	local_irq_disable_hw_notrace()
#endif /* CONFIG_IPIPE_TRACE_IRQSOFF */

#endif /* __ASSEMBLY__ */

/*
 * Do the CPU's IRQ-state tracing from assembly code. We call a
 * C function, so save all the C-clobbered registers:
 */
#ifdef CONFIG_TRACE_IRQFLAGS

# define TRACE_IRQS_ON				\
	pushl %eax;				\
	pushl %ecx;				\
	pushl %edx;				\
	call trace_hardirqs_on;			\
	popl %edx;				\
	popl %ecx;				\
	popl %eax;

# define TRACE_IRQS_OFF				\
	pushl %eax;				\
	pushl %ecx;				\
	pushl %edx;				\
	call trace_hardirqs_off;		\
	popl %edx;				\
	popl %ecx;				\
	popl %eax;

#else
# define TRACE_IRQS_ON
# define TRACE_IRQS_OFF
#endif

#endif
