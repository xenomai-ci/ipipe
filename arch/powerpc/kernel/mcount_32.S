/*
 *  linux/arch/powerpc/kernel/mcount_32.S
 *
 *  Adapted from glibc's ppc32 profiling support --rpm.
 */

#include <asm/page.h>
#include <asm/ppc_asm.h>
#include <asm/reg.h>

/*
 * Excerpt from sysdeps/powerpc/powerpc32/ppc-mcount.S
 *
 * Do profiling as described in the SYSV ELF ABI, _mcount is called
 * with the address of a data word in r0 (that is different for every
 * routine, initialised to 0, and otherwise unused).  The caller has
 * put the address the caller will return to in the usual place on the stack,
 * 4(r1).  _mcount is responsible for ensuring that when it returns no
 * argument-passing registers are disturbed, and that the LR is set back
 * to (what the caller sees as) 4(r1).
 *
 * This is intended so that the following code can be inserted at the
 * front of any routine without changing the routine:
 *
 *	mflr	r0
 *	lis	r12,0b@ha
 *	stw	r0,4(r1)
 *	addi	r0,r12,0b@l
 *	bl	_mcount
 */

.globl	_mcount
_mcount:
	stwu	r1,-48(r1)
/* We need to save the parameter-passing registers.  */
	stw	r3, 12(r1)
	stw	r4, 16(r1)
	stw	r5, 20(r1)
	stw	r6, 24(r1)
	stw	r7, 28(r1)
	stw	r8, 32(r1)
	stw	r9, 36(r1)
	stw	r10,40(r1)
	mflr	r4
	mfcr	r5
	stw	r4, 44(r1)
	stw	r5,  8(r1)
	tophys(r0,0)
	addis	r6,r0,ipipe_trace_enable@ha
	lwz	r3,ipipe_trace_enable@l(r6)
	cmpwi	r3,0
	beq	1f
	li	r3,0
	lwz	r5, 52(r1)
	li	r6,0
	bl	__ipipe_trace
1:	
 /* Restore the registers...  */
	lwz     r6,  8(r1)
	lwz	r0, 44(r1)
	lwz	r3, 12(r1)
	mtctr	r0
	lwz	r4, 16(r1)
	mtcrf	0xff,r6
	lwz	r5, 20(r1)
	lwz	r6, 24(r1)
	lwz	r0, 52(r1)
	lwz	r7, 28(r1)
	lwz	r8, 32(r1)
	mtlr	r0
	lwz	r9, 36(r1)
	lwz	r10,40(r1)
 /* ...unwind the stack frame, and return to your usual programming.  */
	addi	r1,r1,48
	bctr
