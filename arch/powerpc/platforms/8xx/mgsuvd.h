/*
 * A collection of structures, addresses, and values associated with
 * the Keymile mgsuvd board.
 * Copied from the MPC86X stuff.
 *
 * Heiko Schocher <hs@denx.de>
 *
 * Copyright 2007 DENX Software Engineering GmbH
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

#ifdef __KERNEL__
#ifndef __ASM_MGSUVD_H__
#define __ASM_MGSUVD_H__

#include <sysdev/fsl_soc.h>

#define IMAP_ADDR		(get_immrbase())
#define IMAP_SIZE		((uint)(64 * 1024))

#define MPC8xx_CPM_OFFSET	(0x9c0)
#define CPM_MAP_ADDR		(get_immrbase() + MPC8xx_CPM_OFFSET)
#define CPM_IRQ_OFFSET		16     // for compability with cpm_uart driver

/* Interrupt level assignments */

/* We don't use the 8259 */
#define NR_8259_INTS	0

/* CPM Ethernet through SCC3 */
#define PA_ENET_RXD     ((ushort)0x0010)
#define PA_ENET_TXD     ((ushort)0x0020)
#define PA_ENET_TCLK    ((ushort)0x2000)
#define PA_ENET_RCLK    ((ushort)0x1000)
#define PB_ENET_TENA    ((uint)0x00000004)
#define PC_ENET_CLSN    ((ushort)0x0100)
#define PC_ENET_RENA    ((ushort)0x0200)

/* Control bits in the SICR to route TCLK (CLK1) and RCLK (CLK2) to
 * SCC1.  Also, make sure GR1 (bit 24) and SC1 (bit 25) are zero.
 */
#define SICR_ENET_MASK  ((uint)0x00ff0000)
#define SICR_ENET_CLKRT ((uint)0x00250000)

#endif /* __ASM_MGSUVD_H__ */
#endif /* __KERNEL__ */
