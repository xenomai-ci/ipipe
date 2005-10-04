/*
 * $Id: $
 *
 * drivers/mtd/maps/yosemite.c
 *
 * Mapping for Yosemite (440EP) and Yellowstone (440GR) flash
 *
 * Copyright (c) 2005 DENX Software Engineering
 * Stefan Roese <sr@denx.de>
 *
 * Based on original work by
 *      Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>

#define WINDOW_ADDR 0xfc000000
#define WINDOW_SIZE 0x04000000

#define RW_PART0_OF	0
#define RW_PART0_SZ	0x180000
#define RW_PART1_OF	RW_PART0_OF + RW_PART0_SZ
#define RW_PART1_SZ	0x280000
#define RW_PART2_OF	RW_PART1_OF + RW_PART1_SZ
#define RW_PART2_SZ	(WINDOW_SIZE - RW_PART0_SZ - RW_PART1_SZ - RW_PART3_SZ \
			 - RW_PART4_SZ)
#define RW_PART3_OF	RW_PART2_OF + RW_PART2_SZ
#define RW_PART3_SZ	0x40000
#define RW_PART4_OF	RW_PART3_OF + RW_PART3_SZ
#define RW_PART4_SZ	0x80000

static struct mtd_partition yosemite_flash_partitions[] = {
	{
		.name = "kernel",
		.offset = RW_PART0_OF,
		.size = RW_PART0_SZ
	},
	{
		.name = "root",
		.offset = RW_PART1_OF,
		.size = RW_PART1_SZ,
	},
	{
		.name = "user",
		.offset = RW_PART2_OF,
		.size = RW_PART2_SZ
	},
	{
		.name = "env",
		.offset = RW_PART3_OF,
		.size = RW_PART3_SZ,
	},
	{
		.name = "u-boot",
		.offset = RW_PART4_OF,
		.size = RW_PART4_SZ,
	}
};

struct map_info yosemite_flash_map = {
	.name = "AMCC440-flash",
	.size = WINDOW_SIZE,
	.bankwidth = 2,
	.phys = WINDOW_ADDR,
};

#define NUM_YOSEMITE_FLASH_PARTITIONS \
	(sizeof(yosemite_flash_partitions)/sizeof(yosemite_flash_partitions[0]))

static struct mtd_info *yosemite_mtd;

int __init init_yosemite_flash(void)
{
	printk(KERN_NOTICE "yosemite: flash mapping: %x at %x\n",
			WINDOW_SIZE, WINDOW_ADDR);

	yosemite_flash_map.virt = ioremap(WINDOW_ADDR, WINDOW_SIZE);

	if (!yosemite_flash_map.virt) {
		printk("init_yosemite_flash: failed to ioremap\n");
		return -EIO;
	}
	simple_map_init(&yosemite_flash_map);

	yosemite_mtd = do_map_probe("cfi_probe",&yosemite_flash_map);

	if (yosemite_mtd) {
		yosemite_mtd->owner = THIS_MODULE;
		return add_mtd_partitions(yosemite_mtd,
				yosemite_flash_partitions,
				NUM_YOSEMITE_FLASH_PARTITIONS);
	}

	return -ENXIO;
}

static void __exit cleanup_yosemite_flash(void)
{
	if (yosemite_mtd) {
		del_mtd_partitions(yosemite_mtd);
		/* moved iounmap after map_destroy - armin */
		map_destroy(yosemite_mtd);
		iounmap((void *)yosemite_flash_map.virt);
	}
}

module_init(init_yosemite_flash);
module_exit(cleanup_yosemite_flash);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stefan Roese <sr@denx.de>");
MODULE_DESCRIPTION("MTD map and partitions for AMCC 440EP/GR boards");
