/*
 *
 * Utility functions for the Freescale MPC52xx.
 *
 * Copyright (C) 2006 Sylvain Munaut <tnt@246tNt.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

#undef DEBUG

#include <linux/kernel.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <asm/of_platform.h>
#include <asm/mpc52xx.h>


static void __iomem *
mpc52xx_map_node(struct device_node *ofn)
{
	const u32 *regaddr_p;
	u64 regaddr64, size64;

	if (!ofn)
		return NULL;

	regaddr_p = of_get_address(ofn, 0, &size64, NULL);
	if (!regaddr_p) {
		of_node_put(ofn);
		return NULL;
	}

	regaddr64 = of_translate_address(ofn, regaddr_p);

	of_node_put(ofn);

	return ioremap((u32)regaddr64, (u32)size64);
}

void __iomem *
mpc52xx_find_and_map(const char *compatible)
{
	return mpc52xx_map_node(
		of_find_compatible_node(NULL, NULL, compatible));
}

EXPORT_SYMBOL(mpc52xx_find_and_map);

void __iomem *
mpc52xx_find_and_map_path(const char *path)
{
	return mpc52xx_map_node(of_find_node_by_path(path));
}

EXPORT_SYMBOL(mpc52xx_find_and_map_path);

/**
 * 	mpc52xx_find_ipb_freq - Find the IPB bus frequency for a device
 * 	@node:	device node
 *
 * 	Returns IPB bus frequency, or 0 if the bus frequency cannot be found.
 */
unsigned int
mpc52xx_find_ipb_freq(struct device_node *node)
{
	struct device_node *np;
	const unsigned int *p_ipb_freq = NULL;

	of_node_get(node);
	while (node) {
		p_ipb_freq = of_get_property(node, "bus-frequency", NULL);
		if (p_ipb_freq)
			break;

		np = of_get_parent(node);
		of_node_put(node);
		node = np;
	}
	if (node)
		of_node_put(node);

	return p_ipb_freq ? *p_ipb_freq : 0;
}
EXPORT_SYMBOL(mpc52xx_find_ipb_freq);

/*
 * This variable is mapped in mpc52xx_setup_cpu() by a call to
 * mpc52xx_find_and_map(), and used in mpc52xx_restart(). This is because
 * mpc52xx_restart() can be called from interrupt context (e.g., watchdog
 * interrupt handler), and mpc52xx_find_and_map() (ioremap() to be exact)
 * can't be called from interrupt context.
 */
volatile struct mpc52xx_gpt *mpc52xx_gpt0 = NULL;

void __init
mpc52xx_setup_cpu(void)
{
	struct mpc52xx_cdm  __iomem *cdm;
	struct mpc52xx_xlb  __iomem *xlb;

	/* mpc52xx_gpt0 is mapped here and used in mpc52xx_restart */
	mpc52xx_gpt0 = mpc52xx_find_and_map("mpc5200-gpt");

	/* Map zones */
	cdm = mpc52xx_find_and_map("mpc5200-cdm");
	xlb = mpc52xx_find_and_map("mpc5200-xlb");

	if (!cdm || !xlb) {
		printk(KERN_ERR __FILE__ ": "
			"Error while mapping CDM/XLB during mpc52xx_setup_cpu. "
			"Expect some abnormal behavior\n");
		goto unmap_regs;
	}

	/* Use internal 48 Mhz */
	out_8(&cdm->ext_48mhz_en, 0x00);
	out_8(&cdm->fd_enable, 0x01);
	if (in_be32(&cdm->rstcfg) & 0x40)	/* Assumes 33Mhz clock */
		out_be16(&cdm->fd_counters, 0x0001);
	else
		out_be16(&cdm->fd_counters, 0x5555);

	/* Configure the XLB Arbiter priorities */
	out_be32(&xlb->master_pri_enable, 0xff);
	out_be32(&xlb->master_priority, 0x11111111);

	/* Disable XLB pipelining */
	/* (cfr errate 292. We could do this only just before ATA PIO
	    transaction and re-enable it afterwards ...) */
	out_be32(&xlb->config, in_be32(&xlb->config) | MPC52xx_XLB_CFG_PLDIS);

	/* Unmap zones */
unmap_regs:
	if (cdm) iounmap(cdm);
	if (xlb) iounmap(xlb);
}

void __init
mpc52xx_declare_of_platform_devices(void)
{
	/* Find every child of the SOC node and add it to of_platform */
	if (of_platform_bus_probe(NULL, NULL, NULL))
		printk(KERN_ERR __FILE__ ": "
			"Error while probing of_platform bus\n");
}

void
mpc52xx_restart(char *cmd)
{
	local_irq_disable();

	/* Turn on the watchdog and wait for it to expire. It effectively
	  does a reset */
	if (mpc52xx_gpt0) {
		out_be32(&mpc52xx_gpt0->mode, 0x00000000);
		out_be32(&mpc52xx_gpt0->count, 0x000000ff);
		out_be32(&mpc52xx_gpt0->mode, 0x00009004);
	} else
		printk("mpc52xx_restart: Can't access gpt. "
			"Restart impossible, system halted\n");

	while (1);
}

void
mpc52xx_halt(void)
{
	local_irq_disable();

	while (1);
}

void
mpc52xx_power_off(void)
{
	/* By default we don't have any way of shut down.
	   If a specific board wants to, it can set the power down
	   code to any hardware implementation dependent code */
	mpc52xx_halt();
}
