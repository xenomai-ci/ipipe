/*
 * Schindler CM5200 board support
 *
 * Written by: Grant Likely <grant.likely@secretlab.ca>
 * Adapted for CM5200 by: Jan Wrobel <wrr@semihalf.com>
 *
 * Copyright (C) Secret Lab Technologies Ltd. 2006. All rights reserved.
 * Copyright (C) Freescale Semicondutor, Inc. 2006. All rights reserved.
 *
 * Description:
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#undef DEBUG

#include <linux/of.h>
#include <linux/wd.h>
#include <linux/wd_hw.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/mpc52xx.h>

/* ************************************************************************
 *
 * Setup the architecture
 *
 */
static void __init cm5200_setup_cpu(void)
{
#if defined(CONFIG_WD) && defined(CONFIG_WD_MPC5200)
	/* Init watchdog functions structure */
	wd_hw_functions.wd_init = wd_mpc5200_init;
	wd_hw_functions.wd_kick = wd_mpc5200_kick;
	wd_hw_functions.wd_delete = wd_mpc5200_delete;
	wd_hw_functions.wd_machine_restart = wd_mpc5200_machine_restart;
#endif /* CONFIG_WD && CONFIG_WD_MPC5200 */
}

static void __init cm5200_setup_arch(void)
{
	/* Platorm specific */
	cm5200_setup_cpu();

	/* Some mpc5200 & mpc5200b related configuration */
	mpc5200_setup_xlb_arbiter();

	/* Map wdt for mpc52xx_restart() */
	mpc52xx_map_wdt();
}

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init cm5200_probe(void)
{
	unsigned long node = of_get_flat_dt_root();
	const char *model = of_get_flat_dt_prop(node, "model", NULL);

	if (!of_flat_dt_is_compatible(node, "fsl,cm5200"))
		return 0;
	pr_debug("%s board found\n", model ? model : "unknown");

	return 1;
}

define_machine(cm5200) {
	.name		= "cm5200",
	.probe		= cm5200_probe,
	.setup_arch	= cm5200_setup_arch,
	.init		= mpc52xx_declare_of_platform_devices,
	.init_IRQ	= mpc52xx_init_irq,
	.get_irq	= mpc52xx_get_irq,
	.calibrate_decr	= generic_calibrate_decr,
	.restart	= mpc52xx_restart,
};
