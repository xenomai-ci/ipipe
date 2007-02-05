#ifndef __LINUX_PREEMPT_H
#define __LINUX_PREEMPT_H

/*
 * include/linux/preempt.h - macros for accessing and manipulating
 * preempt_count (used for kernel preemption, interrupt count, etc.)
 */

#include <linux/thread_info.h>
#include <linux/linkage.h>

#ifdef CONFIG_DEBUG_PREEMPT
  extern void fastcall add_preempt_count(int val);
  extern void fastcall sub_preempt_count(int val);
#else
# define add_preempt_count(val)	do { preempt_count() += (val); } while (0)
# define sub_preempt_count(val)	do { preempt_count() -= (val); } while (0)
#endif

#define inc_preempt_count() add_preempt_count(1)
#define dec_preempt_count() sub_preempt_count(1)

#define preempt_count()	(current_thread_info()->preempt_count)

#ifdef CONFIG_PREEMPT

asmlinkage void preempt_schedule(void);

#ifdef CONFIG_IPIPE
#include <asm/ipipe.h>
DECLARE_PER_CPU(struct ipipe_domain *, ipipe_percpu_domain);
extern struct ipipe_domain ipipe_root;
#define ipipe_preempt_guard() (per_cpu(ipipe_percpu_domain, ipipe_processor_id()) == &ipipe_root)
#else  /* !CONFIG_IPIPE */
#define ipipe_preempt_guard()	1
#endif /* CONFIG_IPIPE */

#define preempt_disable()						\
do {									\
	if (ipipe_preempt_guard()) {					\
		inc_preempt_count();					\
		barrier();						\
	}								\
} while (0)

#define preempt_enable_no_resched()					\
do {									\
	if (ipipe_preempt_guard()) {					\
		barrier();						\
		dec_preempt_count();					\
	}								\
} while (0)

#define preempt_check_resched()						\
do {									\
	if (ipipe_preempt_guard()) {					\
		if (unlikely(test_thread_flag(TIF_NEED_RESCHED)))	\
			preempt_schedule();				\
	}								\
} while (0)

#define preempt_enable()						\
do {									\
	preempt_enable_no_resched();					\
	barrier(); \
	preempt_check_resched();					\
} while (0)

#else

#define preempt_disable()		do { } while (0)
#define preempt_enable_no_resched()	do { } while (0)
#define preempt_enable()		do { } while (0)
#define preempt_check_resched()		do { } while (0)

#endif

#endif /* __LINUX_PREEMPT_H */
