/*
 * TQM5200 board support
 *
 * Written by: Grant Likely <grant.likely@secretlab.ca> for lite5200
 * Adapted for tqm5200 by: Jan Wrobel <wrr@semihalf.com>
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

#include <linux/pci.h>
#include <linux/of.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/mpc52xx.h>

/* ************************************************************************
 *
 * Setup the architecture
 *
 */

static void __init tqm5200_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("tqm5200_setup_arch()", 0);

	/* Some mpc5200 & mpc5200b related configuration */
	mpc5200_setup_xlb_arbiter();

	/* Map wdt for mpc52xx_restart() */
	mpc52xx_map_wdt();

#ifdef CONFIG_PCI
	np = of_find_node_by_type(NULL, "pci");
	if (np) {
		mpc52xx_add_bridge(np);
		of_node_put(np);
	}
#endif
}

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init tqm5200_probe(void)
{
	unsigned long node = of_get_flat_dt_root();
	const char *model = of_get_flat_dt_prop(node, "model", NULL);

	if (!of_flat_dt_is_compatible(node, "fsl,tqm5200"))
		return 0;
	pr_debug("%s board found\n", model ? model : "unknown");

	return 1;
}

#ifdef CONFIG_RTC_DRV_DS1307
void ds1307_get_rtc_time(struct rtc_time *);
int ds1307_set_rtc_time(struct rtc_time *);
#endif

define_machine(tqm5200) {
	.name		= "tqm5200",
	.probe		= tqm5200_probe,
	.setup_arch	= tqm5200_setup_arch,
	.init		= mpc52xx_declare_of_platform_devices,
	.init_IRQ	= mpc52xx_init_irq,
	.get_irq	= mpc52xx_get_irq,
#ifdef CONFIG_RTC_DRV_DS1307
	.get_rtc_time	= ds1307_get_rtc_time,
	.set_rtc_time	= ds1307_set_rtc_time,
#endif
	.calibrate_decr	= generic_calibrate_decr,
	.restart	= mpc52xx_restart,
};
