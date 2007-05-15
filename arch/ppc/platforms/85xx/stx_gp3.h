/*
 * STx GP3 board definitions
 *
 * Dan Malek (dan@embeddededge.com)
 * Copyright 2004 Embedded Edge, LLC
 *
 * Ported to 2.6, Matt Porter <mporter@kernel.crashing.org>
 * Copyright 2004-2005 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef __MACH_STX_GP3_H
#define __MACH_STX_GP3_H

#include <linux/init.h>
#include <asm/ppcboot.h>
#include <syslib/ppc85xx_setup.h>

#define BOARD_CCSRBAR		((uint)0xe0000000)
#define CCSRBAR_SIZE		((uint)1024*1024)

#define CPM_MAP_ADDR		(CCSRBAR + MPC85xx_CPM_OFFSET)

#define BCSR_ADDR		((uint)0xfc000000)
#define BCSR_SIZE		((uint)(16 * 1024))

#define BCSR_TSEC1_RESET	0x00000080
#define BCSR_TSEC2_RESET	0x00000040
#define BCSR_LED1		0x00000008
#define BCSR_LED2		0x00000004
#define BCSR_LED3		0x00000002
#define BCSR_LED4		0x00000001

/* This is used on the GP3 SSA to prevent probing the IDE1
 * interface that doesn't exist with the onboard IDE controller.
 */
#define IDE_ARCH_OBSOLETE_INIT

extern void mpc85xx_setup_hose(void) __init;
extern void mpc85xx_restart(char *cmd);
extern void mpc85xx_power_off(void);
extern void mpc85xx_halt(void);
extern void mpc85xx_init_IRQ(void) __init;
extern unsigned long mpc85xx_find_end_of_memory(void) __init;
extern void mpc85xx_calibrate_decr(void) __init;

#define PCI1_CFG_ADDR_OFFSET	(0x8000)
#define PCI1_CFG_DATA_OFFSET	(0x8004)

#define PCI2_CFG_ADDR_OFFSET	(0x9000)
#define PCI2_CFG_DATA_OFFSET	(0x9004)

/* PCI 1 memory map */
#define MPC85XX_PCI1_LOWER_IO        0x00000000
#define MPC85XX_PCI1_UPPER_IO        0x00ffffff

#define MPC85XX_PCI1_LOWER_MEM       0x80000000
#define MPC85XX_PCI1_UPPER_MEM       0x9fffffff

#define MPC85XX_PCI1_IO_BASE         0xe2000000
#define MPC85XX_PCI1_MEM_OFFSET      0x00000000

#define MPC85XX_PCI1_IO_SIZE         0x01000000

/* PCI 2 memory map */
/* Note: the standard PPC fixups will cause IO space to get bumped by
 * hose->io_base_virt - isa_io_base => MPC85XX_PCI1_IO_SIZE */
#define MPC85XX_PCI2_LOWER_IO        0x00000000
#define MPC85XX_PCI2_UPPER_IO        0x00ffffff

#define MPC85XX_PCI2_LOWER_MEM       0xa0000000
#define MPC85XX_PCI2_UPPER_MEM       0xbfffffff

#define MPC85XX_PCI2_IO_BASE         0xe3000000
#define MPC85XX_PCI2_MEM_OFFSET      0x00000000

#define MPC85XX_PCI2_IO_SIZE         0x01000000

/* FCC1 Clock Source Configuration.  These can be
 * redefined in the board specific file.
 *    Can only choose from CLK9-12 */
#define F1_RXCLK       12
#define F1_TXCLK       11

/* FCC2 Clock Source Configuration.  These can be
 * redefined in the board specific file.
 *    Can only choose from CLK13-16 */
#define F2_RXCLK       13
#define F2_TXCLK       14

/* FCC3 Clock Source Configuration.  These can be
 * redefined in the board specific file.
 *    Can only choose from CLK13-16 */
#define F3_RXCLK       15
#define F3_TXCLK       16

#endif /* __MACH_STX_GP3_H */
