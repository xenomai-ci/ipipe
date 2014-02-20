#ifndef __ASM_BARRIER_H
#define __ASM_BARRIER_H

#ifndef __ASSEMBLY__
#include <asm/outercache.h>

#define nop() __asm__ __volatile__("mov\tr0,r0\t@ nop\n\t");

#if __LINUX_ARM_ARCH__ >= 7 ||		\
	(__LINUX_ARM_ARCH__ == 6 && defined(CONFIG_CPU_32v6K))
#define sev()	__asm__ __volatile__ ("sev" : : : "memory")
#define wfe()	__asm__ __volatile__ ("wfe" : : : "memory")
#define wfi()	__asm__ __volatile__ ("wfi" : : : "memory")
#endif

#if __LINUX_ARM_ARCH__ >= 7
#define isb() __asm__ __volatile__ ("isb" : : : "memory")
#define dsb() __asm__ __volatile__ ("dsb" : : : "memory")
#define DMB "dmb"
#define DMB_IN
#elif defined(CONFIG_CPU_XSC3) || __LINUX_ARM_ARCH__ == 6
#define isb() __asm__ __volatile__ ("mcr p15, 0, %0, c7, c5, 4" \
				    : : "r" (0) : "memory")
#define dsb() __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 4" \
				    : : "r" (0) : "memory")
#define DMB "mcr p15, 0, %0, c7, c10, 5"
#define DMB_IN "r" (0)
#elif defined(CONFIG_CPU_FA526)
#define isb() __asm__ __volatile__ ("mcr p15, 0, %0, c7, c5, 4" \
				    : : "r" (0) : "memory")
#define dsb() __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 4" \
				    : : "r" (0) : "memory")
#else
#define isb() __asm__ __volatile__ ("" : : : "memory")
#define dsb() __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 4" \
				    : : "r" (0) : "memory")
#endif

#ifdef DMB
#define dmb() __asm__ __volatile__(DMB : : DMB_IN : "memory")
#else
#define dmb() __asm__ __volatile__("" : : : "memory")
#endif


#ifdef CONFIG_ARCH_HAS_BARRIERS
#include <mach/barriers.h>
#elif defined(CONFIG_ARM_DMA_MEM_BUFFERABLE) || defined(CONFIG_SMP)
#define mb()		do { dsb(); outer_sync(); } while (0)
#define rmb()		dsb()
#define wmb()		mb()
#else
#define mb()		barrier()
#define rmb()		barrier()
#define wmb()		barrier()
#endif

#ifndef CONFIG_SMP
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#else
#ifdef CONFIG_THUMB2_KERNEL
#define SMP_ONLY(smp) \
	"9998:	" smp "\n"					\
	"	.pushsection \".alt.smp.init\", \"a\"\n"	\
	"	.long	9998b\n"				\
	"	nop.w\n"					\
	"	.popsection\n"
#else
#define SMP_ONLY(smp) \
	"9998:	" smp "\n"					\
	"	.pushsection \".alt.smp.init\", \"a\"\n"	\
	"	.long	9998b\n"				\
	"	nop\n"					\
	"	.popsection\n"
#endif

#ifdef DMB
#define smp_mb() __asm__ __volatile__ (SMP_ONLY(DMB) : : DMB_IN : "memory")
#define smp_rmb() __asm__ __volatile__ (SMP_ONLY(DMB) : : DMB_IN : "memory")
#define smp_wmb() __asm__ __volatile__ (SMP_ONLY(DMB) : : DMB_IN : "memory")
#else
#define smp_mb() dmb()
#define smp_rmb() dmb()
#define smp_wmb() dmb()
#endif
#endif

#define read_barrier_depends()		do { } while(0)
#define smp_read_barrier_depends()	do { } while(0)

#define set_mb(var, value)	do { var = value; smp_mb(); } while (0)

#endif /* !__ASSEMBLY__ */
#endif /* __ASM_BARRIER_H */
