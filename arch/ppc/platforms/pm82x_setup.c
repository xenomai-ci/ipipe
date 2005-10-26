/*
 * arch/ppc/platforms/pm82x_setup.c
 *
 * PM82X platform support
 *
 * Author: Heiko Schocher <hs@denx.de>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>

#include <asm/mpc8260.h>
#include <asm/cpm2.h>
#include <asm/machdep.h>

static int
pm82x_set_rtc_time(unsigned long time)
{
	((cpm2_map_t *)CPM_MAP_ADDR)->im_sit.sit_tmcnt = time;
	((cpm2_map_t *)CPM_MAP_ADDR)->im_sit.sit_tmcntsc = 0x3;
	return(0);
}

static unsigned long
pm82x_get_rtc_time(void)
{
	return ((cpm2_map_t *)CPM_MAP_ADDR)->im_sit.sit_tmcnt;
}

void __init
m82xx_board_init(void)
{
	/* Anything special for this platform */
	ppc_md.set_rtc_time	= pm82x_set_rtc_time;
	ppc_md.get_rtc_time	= pm82x_get_rtc_time;
}
