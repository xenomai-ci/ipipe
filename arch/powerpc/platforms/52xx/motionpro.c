/*
 * Promess Motion-PRO board support
 *
 * Written by: Grant Likely <grant.likely@secretlab.ca>
 * Adapted for Motion-PRO by: Marian Balakowicz <m8@semihalf.com>
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
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/mpc52xx.h>

/* ************************************************************************
 *
 * Setup the architecture
 *
 */

#ifdef CONFIG_LEDS_MOTIONPRO

/* Initialize GPT register connected to LED. Turn off LED. */
static void motionpro_setup_led(const char *reg_path)
{
	void __iomem *reg_addr;
	u32 reg;

	reg_addr = mpc52xx_find_and_map_path(reg_path);
	if (!reg_addr){
		printk(KERN_ERR __FILE__ ": "
		       "LED setup error: can't map GPIO register %s\n",
		       reg_path);
		return;
	}

	reg = in_be32(reg_addr);
	reg |= MPC52xx_GPT_ENABLE_OUTPUT;
	reg &= ~MPC52xx_GPT_OUTPUT_1;
	out_be32(reg_addr, reg);

	iounmap(reg_addr);
}

/* Initialize Motionpro status and ready LEDs */
static void motionpro_setup_leds(void)
{
	motionpro_setup_led("/soc5200@f0000000/gpt@660");
	motionpro_setup_led("/soc5200@f0000000/gpt@670");
}

#endif

static void __init motionpro_setup_arch(void)
{
#ifdef CONFIG_LEDS_MOTIONPRO
	motionpro_setup_leds();
#endif

	if (ppc_md.progress)
		ppc_md.progress("motionpro_setup_arch()", 0);

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
static int __init motionpro_probe(void)
{
	unsigned long node = of_get_flat_dt_root();
	const char *model = of_get_flat_dt_prop(node, "model", NULL);

	if (!of_flat_dt_is_compatible(node, "fsl,motionpro"))
		return 0;
	pr_debug("%s board found\n", model ? model : "unknown");

	return 1;
}

#ifdef CONFIG_RTC_DRV_DS1307
void ds1307_get_rtc_time(struct rtc_time *);
int ds1307_set_rtc_time(struct rtc_time *);
#endif

define_machine(motionpro) {
	.name		= "motionpro",
	.probe		= motionpro_probe,
	.setup_arch	= motionpro_setup_arch,
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
