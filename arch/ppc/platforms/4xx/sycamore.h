/*
 * arch/ppc/platforms/4xx/sycamore.h
 *
 * Macros, definitions, and data structures specific to the IBM PowerPC
 * 405GPr "Sycamore" evaluation board.
 *
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2000 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_SYCAMORE_H__
#define __ASM_SYCAMORE_H__

#include <linux/config.h>
#include <platforms/4xx/ibm405gpr.h>
#include <asm/ppcboot.h>

/* Memory map for the IBM "Sycamore" 405GP evaluation board.
 * Generic 4xx plus RTC.
 */

#define SYCAMORE_RTC_PADDR	((uint)0xf0000000)
#define SYCAMORE_RTC_VADDR	SYCAMORE_RTC_PADDR
#define SYCAMORE_RTC_SIZE	((uint)8*1024)

#ifdef CONFIG_PPC405GP_INTERNAL_CLOCK
#define BASE_BAUD		201600
#else
#define BASE_BAUD		691200
#endif

#define SYCAMORE_PS2_BASE	0xF0100000
#define SYCAMORE_FPGA_BASE	0xF0300000

#define PPC4xx_MACHINE_NAME	"IBM Sycamore"

#endif /* __ASM_SYCAMORE_H__ */
#endif /* __KERNEL__ */
