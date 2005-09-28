/*
 * arch/ppc/platforms/4xx/walnut.h
 *
 * Macros, definitions, and data structures specific to the IBM PowerPC
 * 405GP "Walnut" evaluation board.
 *
 * Authors: Grant Erickson <grant@lcse.umn.edu>, Frank Rowand
 * <frank_rowand@mvista.com>, Debbie Chu <debbie_chu@mvista.com> or
 * source@mvista.com
 *
 * Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *
 * 2000 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_WALNUT_H__
#define __ASM_WALNUT_H__

#include <linux/config.h>
#include <platforms/4xx/ibm405gp.h>
#include <asm/ppcboot.h>

/* Memory map for the IBM "Walnut" 405GP evaluation board.
 * Generic 4xx plus RTC.
 */

#define WALNUT_RTC_PADDR	((uint)0xf0000000)
#define WALNUT_RTC_VADDR	WALNUT_RTC_PADDR
#define WALNUT_RTC_SIZE		((uint)8*1024)

#ifdef CONFIG_PPC405GP_INTERNAL_CLOCK
#define BASE_BAUD		201600
#else
#define BASE_BAUD		691200
#endif

#define WALNUT_PS2_BASE		0xF0100000

/* Flash */
#define PPC40x_FPGA_BASE		0xF0300000
#define PPC40x_FPGA_REG_OFFS		5	/* offset to flash map reg */
#define PPC40x_FLASH_ONBD_N(x)		(x & 0x02)
#define PPC40x_FLASH_SRAM_SEL(x)	(x & 0x01)
#define PPC40x_FLASH_LOW		0xFFF00000
#define PPC40x_FLASH_HIGH		0xFFF80000
#define PPC40x_FLASH_SIZE		0x80000

#define PPC4xx_MACHINE_NAME	"IBM Walnut"

#endif /* __ASM_WALNUT_H__ */
#endif /* __KERNEL__ */
