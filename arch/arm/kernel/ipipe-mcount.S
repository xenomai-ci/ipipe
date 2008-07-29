/*
 *  linux/arch/arm/kernel/ipipe-mcount.S
 *
 *  Copyright (C) 2006 Sebastian Smolorz <ssmolorz@emlix.com>, emlix GmbH
 */

#ifdef CONFIG_FRAME_POINTER

	.text
	.align 0
	.type mcount %function
	.global mcount

mcount:

	ldr	ip, =ipipe_trace_enable	@ leave early, if disabled
	ldr	ip, [ip]
	cmp	ip, #0
	moveq	pc,lr

	mov	ip,  sp
	stmdb   sp!, {r0 - r3, fp, ip, lr, pc}	@ create stack frame

	mov	r3, #0			@ no additional value (v)
	ldr	r2, [fp, #-4]		@ get lr (the return address
					@ of the caller of the
					@ instrumented function)
	mov	r1, lr			@ get lr - (the return address
					@ of the instrumented function)
	mov	r0, #0			@ IPIPE_TRACE_FN

	sub	fp, ip, #4		@ point fp at this frame

	bl	__ipipe_trace

	ldmdb   fp, {r0 - r3, fp, sp, pc}	@ pop entry frame and return

#endif
