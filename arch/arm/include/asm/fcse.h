/*
 * Filename:    arch/arm/include/asm/fcse.h
 * Description: ARM Process ID (PID) includes for Fast Address Space Switching
 *              (FASS) in ARM Linux.
 * Created:     14/10/2001
 * Changes:     19/02/2002 - Macros added.
 *              03/08/2007 - Adapted to kernel 2.6.21 (ssm)
 *              Feb 2008   - Simplified a bit (rco)
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

/* Sets the CPU's PID Register */
static inline void fcse_pid_set(unsigned long pid)
{
	__asm__ __volatile__ ("mcr p15, 0, %0, c13, c0, 0"
			      : /* */: "r" (pid) : "memory");
}

int fcse_pid_alloc(void);
void fcse_pid_free(unsigned pid);

#else /* ! CONFIG_ARM_FCSE */
#define fcse_pid_set(pid) do { } while (0)
#endif /* ! CONFIG_ARM_FCSE */

#endif /* __ASM_ARM_FCSE_H */
