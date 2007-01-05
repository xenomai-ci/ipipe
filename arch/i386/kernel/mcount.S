/*
 *  linux/arch/i386/mcount.S
 *
 *  Copyright (C) 2005 Jan Kiszka
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

        pushl $0                # no additional value (v)
#ifdef CONFIG_REGPARM
        movl (%ebp),%eax
        movl 0x4(%ebp),%edx     # __CALLER_ADDR0
        movl 0x4(%eax),%ecx     # __CALLER_ADDR1
        movl $0,%eax            # IPIPE_TRACE_FN
        call __ipipe_trace
        popl %eax
#else /* !CONFIG_REGPARM */
        movl (%ebp),%eax
        movl 0x4(%eax),%eax
        pushl %eax              # __CALLER_ADDR1
        movl 0x4(%ebp),%eax
        pushl %eax              # __CALLER_ADDR0
        pushl $0                # IPIPE_TRACE_FN
        call __ipipe_trace
        addl $0x10,%esp
#endif /* CONFIG_REGPARM */

        popl %edx
        popl %ecx
        popl %eax
        popl %ebp
out:
        ret
