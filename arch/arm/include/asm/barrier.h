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
<<<<<<< HEAD
#define isb() __asm__ __volatile__ ("isb" : : : "memory")
#define dsb() __asm__ __volatile__ ("dsb" : : : "memory")
#define DMB "dmb"
#define DMB_IN
=======
#define isb(option) __asm__ __volatile__ ("isb " #option : : : "memory")
#define dsb(option) __asm__ __volatile__ ("dsb " #option : : : "memory")
#define dmb(option) __asm__ __volatile__ ("dmb " #option : : : "memory")
>>>>>>> v3.12
#elif defined(CONFIG_CPU_XSC3) || __LINUX_ARM_ARCH__ == 6
#define isb(x) __asm__ __volatile__ ("mcr p15, 0, %0, c7, c5, 4" \
				    : : "r" (0) : "memory")
#define dsb(x) __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 4" \
				    : : "r" (0) : "memory")
<<<<<<< HEAD
#define DMB "mcr p15, 0, %0, c7, c10, 5"
#define DMB_IN "r" (0)
=======
#define dmb(x) __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 5" \
				    : : "r" (0) : "memory")
>>>>>>> v3.12
#elif defined(CONFIG_CPU_FA526)
#define isb(x) __asm__ __volatile__ ("mcr p15, 0, %0, c7, c5, 4" \
				    : : "r" (0) : "memory")
#define dsb(x) __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 4" \
				    : : "r" (0) : "memory")
<<<<<<< HEAD
=======
#define dmb(x) __asm__ __volatile__ ("" : : : "memory")
>>>>>>> v3.12
#else
#define isb(x) __asm__ __volatile__ ("" : : : "memory")
#define dsb(x) __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 4" \
				    : : "r" (0) : "memory")
<<<<<<< HEAD
=======
#define dmb(x) __asm__ __volatile__ ("" : : : "memory")
>>>>>>> v3.12
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
#define wmb()		do { dsb(st); outer_sync(); } while (0)
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
<<<<<<< HEAD
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
=======
#define smp_mb()	dmb(ish)
#define smp_rmb()	smp_mb()
#define smp_wmb()	dmb(ishst)
>>>>>>> v3.12
#endif

#define read_barrier_depends()		do { } while(0)
#define smp_read_barrier_depends()	do { } while(0)

#define set_mb(var, value)	do { var = value; smp_mb(); } while (0)

#endif /* !__ASSEMBLY__ */
#endif /* __ASM_BARRIER_H */
