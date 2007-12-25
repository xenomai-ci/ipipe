/*
 *  linux/arch/x86/mcount_32.S
 *
 *  Copyright (C) 2005, 2007 Jan Kiszka
 */

.globl mcount
mcount:
	cmpl $0,ipipe_trace_enable
	je out

	pushl %ebp
	movl %esp,%ebp

	pushl %eax
	pushl %ecx
	pushl %edx

	pushl $0		# no additional value (v)
	movl (%ebp),%eax
	movl 0x4(%ebp),%edx	# __CALLER_ADDR0
	movl 0x4(%eax),%ecx	# __CALLER_ADDR1
	movl $0,%eax		# IPIPE_TRACE_FUNC
	call __ipipe_trace
	popl %eax

	popl %edx
	popl %ecx
	popl %eax
	popl %ebp
out:
	ret
