/*   -*- linux-c -*-
 *   arch/x86/include/asm/ipipe_base.h
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
 */

#ifndef __X86_IPIPE_BASE_H
#define __X86_IPIPE_BASE_H

#ifdef CONFIG_X86_32
# include "ipipe_base_32.h"
#else
# include "ipipe_base_64.h"
#endif

#define ex_do_divide_error			0
#define ex_do_debug				1
/* NMI not pipelined. */
#define ex_do_int3				3
#define ex_do_overflow				4
#define ex_do_bounds				5
#define ex_do_invalid_op			6
#define ex_do_device_not_available		7
/* Double fault not pipelined. */
#define ex_do_coprocessor_segment_overrun	9
#define ex_do_invalid_TSS			10
#define ex_do_segment_not_present		11
#define ex_do_stack_segment			12
#define ex_do_general_protection		13
#define ex_do_page_fault			14
#define ex_do_spurious_interrupt_bug		15
#define ex_do_coprocessor_error			16
#define ex_do_alignment_check			17
#define ex_machine_check_vector			18
#define ex_reserved				ex_machine_check_vector
#define ex_do_simd_coprocessor_error		19
#define ex_do_iret_error			32

#if !defined(__ASSEMBLY__) && !defined(CONFIG_SMP)

#if __GNUC__ >= 4
/* Alias to ipipe_root_cpudom_var(status) */
extern unsigned long __ipipe_root_status;
#else
extern unsigned long *const __ipipe_root_status_addr;
#define __ipipe_root_status	(*__ipipe_root_status_addr)
#endif

static inline void __ipipe_stall_root(void)
{
	volatile unsigned long *p = &__ipipe_root_status;
	__asm__ __volatile__("btsl $0,%0;"
			     :"+m" (*p) : : "memory");
}

static inline unsigned long __ipipe_test_and_stall_root(void)
{
	volatile unsigned long *p = &__ipipe_root_status;
	int oldbit;

	__asm__ __volatile__("btsl $0,%1;"
			     "sbbl %0,%0;"
			     :"=r" (oldbit), "+m" (*p)
			     : : "memory");
	return oldbit;
}

static inline unsigned long __ipipe_test_root(void)
{
	volatile unsigned long *p = &__ipipe_root_status;
	int oldbit;

	__asm__ __volatile__("btl $0,%1;"
			     "sbbl %0,%0;"
			     :"=r" (oldbit)
			     :"m" (*p));
	return oldbit;
}

#endif	/* !__ASSEMBLY__ && !CONFIG_SMP */

#endif	/* !__X86_IPIPE_BASE_H */
