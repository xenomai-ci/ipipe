/*
 * include/asm-x86_64/irqflags.h
 *
 * IRQ flags handling
 *
 * This file gets included from lowlevel asm headers too, to provide
 * wrapped versions of the local_irq_*() APIs, based on the
 * raw_local_irq_*() functions from the lowlevel headers.
 */
#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

#ifndef __ASSEMBLY__
/*
 * Interrupt control:
 */

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
		"# __raw_save_flags\n\t"
		"pushfq ; popq %q0"
		: "=g" (flags)
		: /* no input */
		: "memory"
	);
#endif

	return flags;
}

#define raw_local_save_flags(flags) \
		do { (flags) = __raw_local_save_flags(); } while (0)

static inline void raw_local_irq_restore(unsigned long flags)
{
#ifdef CONFIG_IPIPE
	__ipipe_restore_root(!(flags & 0x200));
#else
	__asm__ __volatile__(
		"pushq %0 ; popfq"
		: /* no output */
		:"g" (flags)
		:"memory", "cc"
	);
#endif
}

#ifdef CONFIG_X86_VSMP

/*
 * Interrupt control for the VSMP architecture:
 */

static inline void raw_local_irq_disable(void)
{
	unsigned long flags = __raw_local_save_flags();

	raw_local_irq_restore((flags & ~(1 << 9)) | (1 << 18));
}

static inline void raw_local_irq_enable(void)
{
	unsigned long flags = __raw_local_save_flags();

	raw_local_irq_restore((flags | (1 << 9)) & ~(1 << 18));
}

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & (1<<9)) || (flags & (1 << 18));
}

#else /* CONFIG_X86_VSMP */

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

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & (1 << 9));
}

#endif

/*
 * For spinlocks, etc.:
 */

static inline unsigned long __raw_local_irq_save(void)
{
	unsigned long flags = __raw_local_save_flags();

	raw_local_irq_disable();

	return flags;
}

#define raw_local_irq_save(flags) \
		do { (flags) = __raw_local_irq_save(); } while (0)

static inline int raw_irqs_disabled(void)
{
	unsigned long flags = __raw_local_save_flags();

	return raw_irqs_disabled_flags(flags);
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

#define local_irq_save_hw_notrace(x) \
	__asm__ __volatile__("pushfq ; popq %q0 ; cli":"=g" (x): /* no input */ :"memory")
#define local_irq_restore_hw_notrace(x) \
	__asm__ __volatile__("pushq %0 ; popfq": /* no output */ :"g" (x):"memory", "cc")

#define local_save_flags_hw(x)	__asm__ __volatile__("pushfq ; popq %q0":"=g" (x): /* no input */)

#ifdef CONFIG_X86_VSMP
static inline void local_irq_disable_hw_notrace(void)
{
	unsigned long flags;

	local_save_flags_hw(flags);
	local_irq_restore_hw_notrace((flags & ~(1 << 9)) | (1 << 18));
}
	
static inline void local_irq_enable_hw_notrace(void)
{
	unsigned long flags;

	local_save_flags_hw(flags);
	local_irq_restore_hw_notrace((flags | (1 << 9)) & ~(1 << 18));
}
#else /* !CONFIG_X86_VSMP */
#define local_irq_disable_hw_notrace() \
	__asm__ __volatile__("cli": : :"memory")
#define local_irq_enable_hw_notrace() \
	__asm__ __volatile__("sti": : :"memory")
#endif /* CONFIG_X86_VSMP */

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

#else /* __ASSEMBLY__: */
#ifdef CONFIG_IPIPE_TRACE_IRQSOFF
#define IPIPE_TRACE_IRQS_ON	call __ipipe_trace_irqs_on_thunk
#define IPIPE_TRACE_IRQS_OFF	call __ipipe_trace_irqs_off_thunk
#else /* !CONFIG_IPIPE_TRACE_IRQSOFF */
#define IPIPE_TRACE_IRQS_ON
#define IPIPE_TRACE_IRQS_OFF
#endif /* !CONFIG_IPIPE_TRACE_IRQSOFF */
# ifdef CONFIG_TRACE_IRQFLAGS
#  define TRACE_IRQS_ON	IPIPE_TRACE_IRQS_ON; call trace_hardirqs_on_thunk
#  define TRACE_IRQS_OFF	IPIPE_TRACE_IRQS_OFF; call trace_hardirqs_off_thunk
# else
#  define TRACE_IRQS_ON	IPIPE_TRACE_IRQS_ON
#  define TRACE_IRQS_OFF	IPIPE_TRACE_IRQS_OFF
# endif
#endif

#endif
