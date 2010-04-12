/*
 * Filename:    arch/arm/include/asm/fcse.h
 * Description: ARM Process ID (PID) includes for Fast Address Space Switching
 *              (FASS) in ARM Linux.
 *
 * Copyright:   (C) 2001, 2002 Adam Wiggins <awiggins@cse.unsw.edu.au>
 *              (C) 2007 Sebastian Smolorz <ssm@emlix.com>
 *              (C) 2008 Richard Cochran
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of teh GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARM_FCSE_H
#define __ASM_ARM_FCSE_H

#ifdef CONFIG_ARM_FCSE

#include <linux/mm_types.h>	/* For struct mm_struct */

#define FCSE_PID_SHIFT 25

/* Size of PID relocation area */
#define FCSE_PID_TASK_SIZE (1UL << FCSE_PID_SHIFT)

/* Mask to get rid of PID from relocated address */
#define FCSE_PID_MASK (FCSE_PID_TASK_SIZE - 1)

extern unsigned long fcse_pids_cache_dirty[];

/* Sets the CPU's PID Register */
static inline void fcse_pid_set(unsigned long pid)
{
	__asm__ __volatile__ ("mcr p15, 0, %0, c13, c0, 0"
			      : /* */: "r" (pid) : "cc", "memory");
}

static inline unsigned long
fcse_va_to_mva(struct mm_struct *mm, unsigned long va)
{
	if (va < FCSE_PID_TASK_SIZE)
		return mm->context.fcse.pid | va;
	return va;
}

int fcse_pid_alloc(void);
void fcse_pid_free(struct mm_struct *mm);
void fcse_notify_flush_all(void);
#ifdef CONFIG_ARM_FCSE_BEST_EFFORT
struct fcse_user {
	struct mm_struct *mm;
	unsigned count;
};
extern struct fcse_user fcse_pids_user[];
int fcse_switch_mm(struct mm_struct *prev, struct mm_struct *next);
void fcse_pid_reference(unsigned pid);
void fcse_relocate_mm_to_null_pid(struct mm_struct *mm);
static inline int fcse_mm_in_cache(struct mm_struct *mm)
{
	unsigned fcse_pid = mm->context.fcse.pid >> FCSE_PID_SHIFT;
	return test_bit(fcse_pid, fcse_pids_cache_dirty)
		&& fcse_pids_user[fcse_pid].mm == mm;
}
#else /* CONFIG_ARM_FCSE_GUARANTEED */
static inline int
fcse_switch_mm(struct mm_struct *prev, struct mm_struct *next)
{
	unsigned fcse_pid = next->context.fcse.pid >> FCSE_PID_SHIFT;
	set_bit(fcse_pid, fcse_pids_cache_dirty);
	fcse_pid_set(next->context.fcse.pid);
	return 0;
}
static inline int fcse_mm_in_cache(struct mm_struct *mm)
{
	unsigned fcse_pid = mm->context.fcse.pid >> FCSE_PID_SHIFT;
	return test_bit(fcse_pid, fcse_pids_cache_dirty);
}
#endif /* CONFIG_ARM_FCSE_GUARANTEED */

#else /* ! CONFIG_ARM_FCSE */
#define fcse_pid_set(pid) do { } while (0)
#define fcse_mva_to_va(x) (x)
#define fcse_va_to_mva(mm, x) ({ (void)(mm); (x); })
#define fcse_mm_set_in_cache(mm) do { } while (0)
#define fcse_switch_mm(prev, next) (1)
#define fcse_notify_flush_all() do { } while (0)
#define fcse_mm_in_cache(mm) (cpu_isset(smp_processor_id(), (mm)->cpu_vm_mask))
#endif /* ! CONFIG_ARM_FCSE */

#endif /* __ASM_ARM_FCSE_H */
