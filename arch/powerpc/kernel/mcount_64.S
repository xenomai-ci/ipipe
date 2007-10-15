/*
 *  linux/arch/powerpc/kernel/mcount_64.S
 *
 * Adapted from Adam Litke's mcount() support for ppc64
 * (agl@us.ibm.com) --rpm
 */

#include <asm/ppc_asm.h>
#include <asm/reg.h>

_GLOBAL(_mcount)
	std	r3, -16(r1)
        LOAD_REG_IMMEDIATE(r3, ipipe_trace_enable)
        lwz     r3,0(r3)
        cmpwi   r3,0
        beq     2f
	std	r4, -24(r1)
	std	r5, -32(r1)
	std	r6, -40(r1)
	std	r7, -48(r1)
	std	r8, -56(r1)
	std	r9, -64(r1)
	std	r10, -72(r1)

	mflr	r0
	std	r0, 16(r1)
	mfcr	r0
	std	r0, 8(r1)
        ld      r5, 0(r1)
	stdu	r1, -184(r1)

	/* If relocation is off, skip tracing. */
	std	r14, -8(r1)
	mfmsr	r14
	andi.	r14, r14, MSR_IR
	cmpldi	r14, 0
	ld	r14, -8(r1)
	beq	1f

        li      r3, 0
        mr	r4, r0		/* caller_eip */
        ld      r5, 16(r5)	/* parent_eip */
        li      r6,0
	bl	__ipipe_trace
	nop

1:
	addi	r1, r1, 184
	ld	r0, 16(r1)
	mtlr	r0
	ld	r0, 8(r1)
	mtcr	r0
	ld	r4, -24(r1)
	ld	r5, -32(r1)
	ld	r6, -40(r1)
	ld	r7, -48(r1)
	ld	r8, -56(r1)
	ld	r9, -64(r1)
	ld	r10, -72(r1)
2:	
	ld	r3, -16(r1)
	blr
