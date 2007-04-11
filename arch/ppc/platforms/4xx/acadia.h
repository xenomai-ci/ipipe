/*
 * Acadia board definitions
 *
 * Copyright 2007 DENX Software Engineering, Stefan Roese <sr@denx.de>
 *
 * Copyright 2006 AMCC (www.amcc.com)
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifdef __KERNEL__
#ifndef __ASM_ACADIA_H__
#define __ASM_ACADIA_H__

#include <platforms/4xx/ppc405ez.h>
#include <asm/ppcboot.h>

#define BOARD_SYSCLK		66666666

/* The UART clock is based off an internal clock -
 * define BASE_BAUD based on the internal clock and divider(s).
 * Since BASE_BAUD must be a constant, we will initialize it
 * using clock/divider values which U-Boot initializes
 * for typical configurations at various CPU speeds.
 */
#define BASE_BAUD		0

#define PPC4xx_MACHINE_NAME	"AMCC Acadia"

#endif /* __ASM_ACADIA_H__ */
#endif /* __KERNEL__ */
