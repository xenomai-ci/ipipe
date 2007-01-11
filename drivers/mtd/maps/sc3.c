/*
 * drivers/mtd/maps/sc3.c
 *
 * Mapping for SC3 flash
 *
 * Copyright (c) 2007 DENX Software Engineering
 * Heiko Schocher <hs@denx.de>
 *
 * Based on original work by
 * 	Stefan Roese <sr@denx.de>
 * 	Heikki Lindholm <holindho@infradead.org>
 *      Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <asm/io.h>
#include <asm/ibm4xx.h>

static struct mtd_info *flash;

static struct map_info sc3_map = {
	.name =		"SC3-flash",
	.size =		SC3_FLASH_SIZE,
	.bankwidth =	1,
};

static struct mtd_partition sc3_partitions[] = {
	{
		.name =   "env",
		.offset = 0,
		.size =   0x40000,
	},
	{
		.name =   "data",
		.offset = 0x40000,
		.size =   0x180000,
	},
	{
		.name =   "u-boot",
		.offset = 0x1c0000,
		.size =   0x40000,
	}
};

int __init init_sc3(void)
{
	unsigned long flash_base = SC3_FLASH_BASE;

	sc3_map.phys = flash_base;
	sc3_map.virt =
		(void __iomem *)ioremap(flash_base, sc3_map.size);

	if (!sc3_map.virt) {
		printk("Failed to ioremap flash.\n");
		return -EIO;
	}

	simple_map_init(&sc3_map);

	flash = do_map_probe("cfi_probe", &sc3_map);
	if (flash) {
		flash->owner = THIS_MODULE;
		add_mtd_partitions(flash, sc3_partitions,
					ARRAY_SIZE(sc3_partitions));
	} else {
		printk("map probe failed for flash\n");
		return -ENXIO;
	}

	return 0;
}

static void __exit cleanup_sc3(void)
{
	if (flash) {
		del_mtd_partitions(flash);
		map_destroy(flash);
	}

	if (sc3_map.virt) {
		iounmap((void *)sc3_map.virt);
		sc3_map.virt = 0;
	}
}

module_init(init_sc3);
module_exit(cleanup_sc3);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Heiko Schocher <hs@denx.de>");
MODULE_DESCRIPTION("MTD map and partitions for the SC3 board");
