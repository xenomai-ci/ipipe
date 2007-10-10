/*
 * drivers/mtd/maps/tqm5200.c
 *
 * Adapted to TQM5200 in 2.6 by Jan Wrobel <wrr@semihalf.com>
 * Based on MTD mapping drivers:
 *   for Motion-PRO by Piotr Kruszynski,
 *   for TQM5200 in 2.4 kernel by Wolfgang Denx.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 * MTD mapping driver for TQM5200 board
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/io.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#define WINDOW_ADDR	0xfc000000
#define WINDOW_SIZE	0x02000000

/*
 * Flash partitions definition
 *
 * NOTE: Any partition can be configured as read-only by setting
 * MTD_WRITEABLE in mask_flags field.
 */
static struct mtd_partition tqm5200_partitions[] = {
	{
		name:	"firmware",		/* U-Boot Firmware */
		offset:	0x00000000,
		size:	0x000A0000,
	}, {
		name:	"dtb",			/* Flattened device tree blob */
		offset:	0x000A0000,
		size:	0x00020000,
	}, {
		name:	"kernel",		/* default kernel image */
		offset:	0x000C0000,
		size:	0x00240000,
	}, {
		name:	"initrd",		/* RAMdisk image */
		offset:	0x00300000,
		size:	0x00200000,
	}, {
		name:	"small-fs",		/* Small filesystem */
		offset:	0x00500000,
		size:	0x00400000,
	}, {
		name:	"misc",			/* Miscellanous space */
		offset:	0x00900000,
		size:	0x00800000,
	}, {
		name:	"big-fs",		/* Big filesystem */
		offset:	0x01100000,
		size:	0x00f00000,
	},
};

static struct map_info tqm5200_flash_map = {
	.name		= "TQM5200-0",
	.size		= WINDOW_SIZE,
	.bankwidth	= 4,
	.phys		= WINDOW_ADDR,
};

static struct mtd_info		*tqm5200_mtd = NULL;
static struct mtd_partition	*tqm5200_parts = NULL;

static int __init init_tqm5200_flash(void)
{
#ifdef CONFIG_MTD_CMDLINE_PARTS
	const char *probes[] = { "cmdlinepart", NULL };
#endif
	int err = 0;
	int n;

	tqm5200_flash_map.virt = ioremap(WINDOW_ADDR, WINDOW_SIZE);
	if (tqm5200_flash_map.virt == NULL) {
		printk("init_tqm5200_flash: error remapping flash memory\n");
		return -EIO;
	}
	simple_map_init(&tqm5200_flash_map);

	tqm5200_mtd = do_map_probe("cfi_probe", &tqm5200_flash_map);
	if (tqm5200_mtd == NULL) {
		printk("init_tqm5200_flash: cfi probe failed\n");
		err = -ENXIO;
		goto fail;
	}

	tqm5200_mtd->owner = THIS_MODULE;
#ifdef CONFIG_MTD_CMDLINE_PARTS
	n = parse_mtd_partitions(tqm5200_mtd, probes, &tqm5200_parts, 0);
	printk("TQM5200 MTD map: Using %s definition of flash partitions\n",
	       ((n <= 0) ? "static" : "cmdline"));
	if (n <= 0)
#endif
	{
		tqm5200_parts = tqm5200_partitions;
		n = ARRAY_SIZE(tqm5200_partitions);
	}

	err = add_mtd_partitions(tqm5200_mtd, tqm5200_parts, n);
	if (err) {
		printk("init_tqm5200_flash: add_mtd_partitions failed\n");
		map_destroy(tqm5200_mtd);
		tqm5200_mtd = NULL;
	}

fail:
	if (err) {
#ifdef CONFIG_MTD_CMDLINE_PARTS
		if (tqm5200_parts != tqm5200_partitions &&
				tqm5200_parts != NULL)
			kfree(tqm5200_parts);
#endif
		tqm5200_parts = NULL;
		iounmap(tqm5200_flash_map.virt);
	}
	return (err);
}

static void __exit cleanup_tqm5200_flash(void)
{
	if (tqm5200_mtd == NULL)
		return;

	del_mtd_partitions(tqm5200_mtd);
#ifdef CONFIG_MTD_CMDLINE_PARTS
	if (tqm5200_parts != tqm5200_partitions &&
			tqm5200_parts != NULL)
		kfree(tqm5200_parts);
#endif
	tqm5200_parts = NULL;
	map_destroy(tqm5200_mtd);
	tqm5200_mtd = NULL;

	iounmap(tqm5200_flash_map.virt);
}

module_init(init_tqm5200_flash);
module_exit(cleanup_tqm5200_flash);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MTD map driver for TQM5200 board");
