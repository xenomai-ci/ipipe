#ifndef __LINUX_PREEMPT_H
#define __LINUX_PREEMPT_H

/*
 * include/linux/preempt.h - macros for accessing and manipulating
 * preempt_count (used for kernel preemption, interrupt count, etc.)
 */

#include <linux/thread_info.h>
#include <linux/linkage.h>
#include <linux/ipipe_base.h>

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

#define preempt_disable() \
do { \
	ipipe_check_context(ipipe_root_domain); \
	inc_preempt_count(); \
	barrier(); \
} while (0)

#define preempt_enable_no_resched() \
do { \
	ipipe_check_context(ipipe_root_domain); \
	barrier(); \
	dec_preempt_count(); \
} while (0)

#define preempt_check_resched() \
do { \
	ipipe_check_context(ipipe_root_domain); \
	if (unlikely(test_thread_flag(TIF_NEED_RESCHED))) \
		preempt_schedule(); \
} while (0)

#define preempt_enable() \
do { \
	preempt_enable_no_resched(); \
	barrier(); \
	preempt_check_resched(); \
} while (0)

#else

#define preempt_disable()		ipipe_check_context(ipipe_root_domain)
#define preempt_enable_no_resched()	ipipe_check_context(ipipe_root_domain)
#define preempt_enable()		ipipe_check_context(ipipe_root_domain)
#define preempt_check_resched()		ipipe_check_context(ipipe_root_domain)

#endif

#endif /* __LINUX_PREEMPT_H */
