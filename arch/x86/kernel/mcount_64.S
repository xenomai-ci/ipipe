/*
 *   linux/arch/x86/kernel/mcount_64.S
 *
 *   Copyright (C) 2002 Free Software Foundation, Inc.
 *   Contributed by Andreas Jaeger <aj@suse.de>.
 *   Slightly adapted by Philippe Gerum for the I-pipe tracer.
 */

#include <linux/linkage.h>

	.code64

ENTRY(mcount)
	cmpl	$0,ipipe_trace_enable
	jz	out
	subq	$56,%rsp
	movq	%rax,(%rsp)
	movq	%rcx,8(%rsp)
	movq	%rdx,16(%rsp)
	movq	%rsi,24(%rsp)
	movq	%rdi,32(%rsp)
	movq	%r8,40(%rsp)
	movq	%r9,48(%rsp)

	movq	$0,%rcx		/* No additional value. */
	movq	8(%rbp),%rdx    /* Parent rip. */
	movq	56(%rsp),%rsi   /* Caller rip. */
	movq	$0,%rdi		/* IPIPE_TRACE_FN */
	call    __ipipe_trace

	movq	48(%rsp),%r9
	movq	40(%rsp),%r8
	movq	32(%rsp),%rdi
	movq	24(%rsp),%rsi
	movq	16(%rsp),%rdx
	movq	8(%rsp),%rcx
	movq	(%rsp),%rax
	addq	$56,%rsp
out:	
	ret
END(mcount)
