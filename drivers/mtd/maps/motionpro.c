/*
 * drivers/mtd/maps/motionpro.c
 *
 * (C) Copyright 2007 DENX Software Engineering
 *
 * Author: Piotr Kruszynski <ppk@semihalf.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 * MTD mapping driver for Motion-PRO board
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/io.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#define WINDOW_ADDR	0xff000000
#define WINDOW_SIZE	0x01000000

/*
 * Flash partitions definition
 *
 * NOTE: Any partition can be configured as read-only by setting
 * MTD_WRITEABLE in mask_flags field.
 */
static struct mtd_partition motionpro_flash_partitions[] = {
	{
		.name		= "fs",
		.offset		= 0x00000000,
		.size		= 0x00d00000,
	},
	{
		.name		= "kernel",
		.offset		= 0x00d00000,
		.size		= 0x00200000,
	},
	{
		.name		= "uboot",
		.offset		= 0x00f00000,
		.size		= 0x00040000,
	},
	{
		.name		= "env",
		.offset		= 0x00f40000,
		.size		= 0x00010000,
	},
	{
		.name		= "dtb",
		.offset		= 0x00f50000,
		.size		= 0x00010000,
	},
	{
		.name		= "user_data",
		.offset		= 0x00f60000,
		.size		= 0x000a0000,
	}
};

static struct map_info motionpro_flash_map = {
	.name		= "Motion-PRO flash",
	.size		= WINDOW_SIZE,
	.bankwidth	= 2,
	.phys		= WINDOW_ADDR,
};

static struct mtd_info *motionpro_mtd = NULL;

static int __init init_motionpro_flash(void)
{
	int err = 0;

	motionpro_flash_map.virt = ioremap(WINDOW_ADDR, WINDOW_SIZE);
	if (motionpro_flash_map.virt == NULL) {
		printk("init_motionpro_flash: error remapping flash memory\n");
		return -EIO;
	}
	simple_map_init(&motionpro_flash_map);

	motionpro_mtd = do_map_probe("cfi_probe", &motionpro_flash_map);
	if (motionpro_mtd == NULL) {
		printk("init_motionpro_flash: cfi probe failed\n");
		err = -ENXIO;
		goto fail;
	}

	motionpro_mtd->owner = THIS_MODULE;
	err = add_mtd_partitions(motionpro_mtd,
			motionpro_flash_partitions,
			ARRAY_SIZE(motionpro_flash_partitions));
	if (err) {
		printk("init_motionpro_flash: add_mtd_partitions failed\n");
		map_destroy(motionpro_mtd);
		motionpro_mtd = NULL;
	}

fail:
	if (err)
		iounmap(motionpro_flash_map.virt);
	return (err);
}

static void __exit cleanup_motionpro_flash(void)
{
	if (motionpro_mtd == NULL)
		return;

	del_mtd_partitions(motionpro_mtd);
	map_destroy(motionpro_mtd);
	motionpro_mtd = NULL;

	iounmap(motionpro_flash_map.virt);
}

module_init(init_motionpro_flash);
module_exit(cleanup_motionpro_flash);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Piotr Kruszynski <ppk@semihalf.com>");
MODULE_DESCRIPTION("MTD map driver for Motion-PRO board");
