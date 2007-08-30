/*
 * File:        include/asm/system.h
 * Based on:
 * Author:      Tony Kou (tonyko@lineo.ca)
 *              Copyright (c) 2002 Arcturus Networks Inc.
 *                    (www.arcturusnetworks.com)
 *              Copyright (c) 2003 Metrowerks (www.metrowerks.com)
 *              Copyright (c) 2004 Analog Device Inc.
 * Created:     25Jan2001 - Tony Kou
 * Description: system.h include file
 *
 * Modified:     22Sep2006 - Robin Getz
 *                - move include blackfin.h down, so I can get access to
 *                   irq functions in other include files.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.
 * If not, write to the Free Software Foundation,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _BLACKFIN_SYSTEM_H
#define _BLACKFIN_SYSTEM_H

#include <linux/linkage.h>
#include <linux/compiler.h>
#include <asm/mach/anomaly.h>

/*
 * Interrupt configuring macros.
 */

extern unsigned long irq_flags;

#ifdef CONFIG_IPIPE

#include <linux/ipipe_trace.h>

void __ipipe_stall_root(void);

void __ipipe_unstall_root(void);

unsigned long __ipipe_test_root(void);

unsigned long __ipipe_test_and_stall_root(void);

void __ipipe_restore_root(unsigned long flags);

#ifdef CONFIG_DEBUG_HWERR
#define __all_masked_irq_flags  0x3f
#define __save_and_cli_hw(x) do {		\
	__asm__ __volatile__ (			\
		"cli %0;\n\tsti %1;"		\
		:"=&d"(x): "d" (0x3F));		\
} while (0)
#else
#define __all_masked_irq_flags  0x1f
#define __save_and_cli_hw(x) do {	\
	__asm__ __volatile__ (          \
		"cli %0;"		\
		:"=&d"(x):);		\
} while (0)
#endif

#define irqs_enabled_from_flags_hw(x)	((x) != __all_masked_irq_flags)
#define raw_irqs_disabled_flags(flags)	(!irqs_enabled_from_flags_hw(flags))
#define local_test_iflag_hw(x)		irqs_enabled_from_flags_hw(x)

#define local_save_flags(x)						\
	do {								\
		(x) = __ipipe_test_root()?__all_masked_irq_flags:irq_flags; \
	} while(0)

#define local_irq_save(x)				\
	do {						\
		(x) = __ipipe_test_and_stall_root();	\
	} while(0)

#define local_irq_restore(x)	__ipipe_restore_root(x)
#define local_irq_disable()	__ipipe_stall_root()
#define local_irq_enable()	__ipipe_unstall_root()
#define irqs_disabled()		__ipipe_test_root()

#define local_save_flags_hw(x) asm volatile ("cli %0;"  \
					     "sti %0;"	\
					     :"=d"(x):)

#define	irqs_disabled_hw()				\
	({						\
		unsigned long flags;			\
		local_save_flags_hw(flags);		\
		!irqs_enabled_from_flags_hw(flags);	\
	})

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

#ifdef CONFIG_IPIPE_TRACE_IRQSOFF

#define local_irq_disable_hw()						\
	do {								\
		int _tmp_dummy;						\
		if (!irqs_disabled_hw())				\
			ipipe_trace_begin(0x80000000);			\
		__asm__ __volatile__ ( "cli %0;":"=d" (_tmp_dummy):);	\
	} while (0)

#define local_irq_enable_hw()						\
	do {								\
		if (irqs_disabled_hw())					\
			ipipe_trace_end(0x80000000);			\
		__asm__ __volatile__ ("sti %0;"::"d"(irq_flags));	\
	} while (0)

#define local_irq_save_hw(x)				\
	do {						\
		__save_and_cli_hw(x);			\
		if (local_test_iflag_hw(x))		\
			ipipe_trace_begin(0x80000001);	\
	} while (0)

#define local_irq_restore_hw(x)				\
	do {						\
		if (local_test_iflag_hw(x)) {		\
			ipipe_trace_end(0x80000001);	\
			local_irq_enable_hw_notrace();	\
		}					\
	} while (0)

#define local_irq_disable_hw_notrace()					\
	do {								\
		int _tmp_dummy;						\
		__asm__ __volatile__ ( "cli %0;":"=d" (_tmp_dummy):);	\
	} while(0)

#define local_irq_enable_hw_notrace()				\
	__asm__ __volatile__ ("sti %0;"::"d"(irq_flags))

#define local_irq_save_hw_notrace(x) __save_and_cli_hw(x)

#define local_irq_restore_hw_notrace(x)			\
	do {						\
		if (local_test_iflag_hw(x))		\
			local_irq_enable_hw_notrace();	\
	} while (0)

#else /* CONFIG_IPIPE_TRACE_IRQSOFF */

#define local_irq_enable_hw()			\
	do {					\
		__asm__ __volatile__ (		\
			"sti %0;"		\
			::"d"(irq_flags));	\
	} while(0)

#define local_irq_disable_hw()			\
	do {					\
		int _tmp_dummy;			\
		__asm__ __volatile__ (		\
			"cli %0;"		\
			:"=d" (_tmp_dummy):);	\
	} while(0)

#define local_irq_restore_hw(x) do {			\
		if (irqs_enabled_from_flags_hw(x))	\
			local_irq_enable_hw();		\
	} while (0)

#define local_irq_save_hw(x)		__save_and_cli_hw(x)

#define local_irq_disable_hw_notrace()	local_irq_disable_hw()
#define local_irq_enable_hw_notrace()	local_irq_enable_hw()
#define local_irq_save_hw_notrace(x)	local_irq_save_hw(x)
#define local_irq_restore_hw_notrace(x)	local_irq_restore_hw(x)

#endif  /* CONFIG_IPIPE_TRACE_IRQSOFF */

#else /* !CONFIG_IPIPE */

#define local_irq_enable() \
	__asm__ __volatile__( \
		"sti %0;" \
		: \
		: "d" (irq_flags) \
	)

#define local_irq_disable() \
	do { \
		int __tmp_dummy; \
		__asm__ __volatile__( \
			"cli %0;" \
			: "=d" (__tmp_dummy) \
		); \
	} while (0)

#ifdef CONFIG_DEBUG_HWERR
# define __save_and_cli(x) \
	__asm__ __volatile__( \
		"cli %0;" \
		"sti %1;" \
		: "=&d" (x) \
		: "d" (0x3F) \
	)
#else
# define __save_and_cli(x) \
	__asm__ __volatile__( \
		"cli %0;" \
		: "=&d" (x) \
	)
#endif

#define local_save_flags(x) \
	__asm__ __volatile__( \
		"cli %0;" \
		"sti %0;" \
		: "=d" (x) \
	)

#ifdef CONFIG_DEBUG_HWERR
#define irqs_enabled_from_flags(x) (((x) & ~0x3f) != 0)
#else
#define irqs_enabled_from_flags(x) ((x) != 0x1f)
#endif

#define local_irq_restore(x) \
	do { \
		if (irqs_enabled_from_flags(x)) \
			local_irq_enable(); \
	} while (0)

/* For spinlocks etc */
#define local_irq_save(x) __save_and_cli(x)

#define	irqs_disabled()				\
({						\
	unsigned long flags;			\
	local_save_flags(flags);		\
	!irqs_enabled_from_flags(flags);	\
})

#define local_irq_save_hw(x)		local_irq_save(x)
#define local_irq_restore_hw(x)		local_irq_restore(x)
#define local_irq_enable_hw()		local_irq_enable()
#define local_irq_disable_hw()		local_irq_disable()
#define irqs_disabled_hw()		irqs_disabled()

#endif /* !CONFIG_IPIPE */

#if ANOMALY_05000244 && defined(CONFIG_BFIN_ICACHE)
# define NOP_PAD_ANOMALY_05000244 "nop; nop;"
#else
# define NOP_PAD_ANOMALY_05000244
#endif

#define idle_with_irq_disabled() \
	__asm__ __volatile__( \
		NOP_PAD_ANOMALY_05000244 \
		".align 8;" \
		"sti %0;" \
		"idle;" \
		: \
		: "d" (irq_flags) \
	)

/*
 * Force strict CPU ordering.
 */
#define nop()  asm volatile ("nop;\n\t"::)
#define mb()   asm volatile (""   : : :"memory")
#define rmb()  asm volatile (""   : : :"memory")
#define wmb()  asm volatile (""   : : :"memory")
#define set_rmb(var, value)    do { (void) xchg(&var, value); } while (0)
#define set_mb(var, value)     set_rmb(var, value)
#define set_wmb(var, value)    do { var = value; wmb(); } while (0)

#define read_barrier_depends() 		do { } while(0)

#ifdef CONFIG_SMP
#define smp_mb()	mb()
#define smp_rmb()	rmb()
#define smp_wmb()	wmb()
#define smp_read_barrier_depends()	read_barrier_depends()
#else
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define smp_read_barrier_depends()	do { } while(0)
#endif

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

struct __xchg_dummy {
	unsigned long a[100];
};
#define __xg(x) ((volatile struct __xchg_dummy *)(x))

static inline unsigned long __xchg(unsigned long x, volatile void *ptr,
				   int size)
{
	unsigned long tmp = 0;
	unsigned long flags = 0;

	local_irq_save_hw(flags);

	switch (size) {
	case 1:
		__asm__ __volatile__
			("%0 = b%2 (z);\n\t"
			 "b%2 = %1;\n\t"
			 : "=&d" (tmp) : "d" (x), "m" (*__xg(ptr)) : "memory");
		break;
	case 2:
		__asm__ __volatile__
			("%0 = w%2 (z);\n\t"
			 "w%2 = %1;\n\t"
			 : "=&d" (tmp) : "d" (x), "m" (*__xg(ptr)) : "memory");
		break;
	case 4:
		__asm__ __volatile__
			("%0 = %2;\n\t"
			 "%2 = %1;\n\t"
			 : "=&d" (tmp) : "d" (x), "m" (*__xg(ptr)) : "memory");
		break;
	}
	local_irq_restore_hw(flags);
	return tmp;
}

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */
static inline unsigned long __cmpxchg(volatile void *ptr, unsigned long old,
				      unsigned long new, int size)
{
	unsigned long tmp = 0;
	unsigned long flags = 0;

	local_irq_save_hw(flags);

	switch (size) {
	case 1:
		__asm__ __volatile__
			("%0 = b%3 (z);\n\t"
			 "CC = %1 == %0;\n\t"
			 "IF !CC JUMP 1f;\n\t"
			 "b%3 = %2;\n\t"
			 "1:\n\t"
			 : "=&d" (tmp) : "d" (old), "d" (new), "m" (*__xg(ptr)) : "memory");
		break;
	case 2:
		__asm__ __volatile__
			("%0 = w%3 (z);\n\t"
			 "CC = %1 == %0;\n\t"
			 "IF !CC JUMP 1f;\n\t"
			 "w%3 = %2;\n\t"
			 "1:\n\t"
			 : "=&d" (tmp) : "d" (old), "d" (new), "m" (*__xg(ptr)) : "memory");
		break;
	case 4:
		__asm__ __volatile__
			("%0 = %3;\n\t"
			 "CC = %1 == %0;\n\t"
			 "IF !CC JUMP 1f;\n\t"
			 "%3 = %2;\n\t"
			 "1:\n\t"
			 : "=&d" (tmp) : "d" (old), "d" (new), "m" (*__xg(ptr)) : "memory");
		break;
	}
	local_irq_restore_hw(flags);
	return tmp;
}

#define cmpxchg(ptr,o,n)\
        ((__typeof__(*(ptr)))__cmpxchg((ptr),(unsigned long)(o),\
                                        (unsigned long)(n),sizeof(*(ptr))))

#define prepare_to_switch()     do { } while(0)

/*
 * switch_to(n) should switch tasks to task ptr, first checking that
 * ptr isn't the current task, in which case it does nothing.
 */

#include <asm/blackfin.h>

asmlinkage struct task_struct *resume(struct task_struct *prev, struct task_struct *next);

#define switch_to(prev,next,last) \
do {    \
	memcpy (&task_thread_info(prev)->l1_task_info, L1_SCRATCH_TASK_INFO, \
		sizeof *L1_SCRATCH_TASK_INFO); \
	memcpy (L1_SCRATCH_TASK_INFO, &task_thread_info(next)->l1_task_info, \
		sizeof *L1_SCRATCH_TASK_INFO); \
	(last) = resume (prev, next);   \
} while (0)

#endif				/* _BLACKFIN_SYSTEM_H */
