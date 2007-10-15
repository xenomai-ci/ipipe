/*
 * Copyright (C) 2006-2007 PA Semi, Inc
 *
 * Author: Egor Martovetsky <egor@pasemi.com>
 * Maintained by: Olof Johansson <olof@lixom.net>
 *
 * Driver for the PWRficient onchip NAND flash interface
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/platform_device.h>
#include <asm/of_platform.h>

#include <asm/io.h>

#define LBICTRL_LPCCTL			0xfc801100
#define   LBICTRL_LPCCTL_NR		0x00004000
#define CLE_PIN_CTL			15
#define ALE_PIN_CTL			14

static void __iomem *lpcctl;
static struct mtd_info *pasemi_nand_mtd;
static const char driver_name[] = "pasemi-nand";

static void pasemi_read_buf(struct mtd_info *mtd, u_char *buf, int len)
{
	struct nand_chip *this = mtd->priv;

	while (len > 0x800) {
		memcpy_fromio(buf, this->IO_ADDR_R, 0x800);
		buf += 0x800;
		len -= 0x800;
	}
	memcpy_fromio(buf, this->IO_ADDR_R, len);
}

static void pasemi_write_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	struct nand_chip *this = mtd->priv;

	while (len > 0x800) {
		memcpy_toio(this->IO_ADDR_R, buf, 0x800);
		buf += 0x800;
		len -= 0x800;
	}
	memcpy_toio(this->IO_ADDR_R, buf, len);
}

static void pasemi_hwcontrol(struct mtd_info *mtd, int cmd,
			     unsigned int ctrl)
{
	struct nand_chip *this = mtd->priv;

	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		writeb(cmd, this->IO_ADDR_W + (1 << CLE_PIN_CTL));
	else
		writeb(cmd, this->IO_ADDR_W + (1 << ALE_PIN_CTL));
}

static int pasemi_device_ready(struct mtd_info *mtd)
{
	return !!(in_le32(lpcctl) & LBICTRL_LPCCTL_NR);
}

static int __devinit pasemi_nand_probe(struct of_device *ofdev,
				      const struct of_device_id *match)
{
	struct device_node *np = ofdev->node;
	struct resource res;
	struct nand_chip *this;
	int err = 0;

	err = of_address_to_resource(np, 0, &res);

	if (err)
		return -EINVAL;

	/* Allocate memory for MTD device structure and private data */
	if (!pasemi_nand_mtd)
		pasemi_nand_mtd = kzalloc(sizeof(struct mtd_info) +
					 sizeof(struct nand_chip), GFP_KERNEL);
	if (!pasemi_nand_mtd) {
		printk(KERN_WARNING
		       "Unable to allocate PASEMI NAND MTD device structure\n");
		err = -ENOMEM;
		goto out;
	}

	/* Get pointer to private data */
	this = (struct nand_chip *)(&pasemi_nand_mtd[1]);

	/* Link the private data with the MTD structure */
	pasemi_nand_mtd->priv = this;
	pasemi_nand_mtd->owner = THIS_MODULE;

	/* Map physical address */
	this->IO_ADDR_R = this->IO_ADDR_W = ioremap(res.start,0x10000);

	if (!this->IO_ADDR_R) {
		err = -EIO;
		goto out_mtd;
	}

	lpcctl = ioremap(LBICTRL_LPCCTL,4);
	if (!lpcctl) {
		err = -EIO;
		goto out_ior;
	}

	this->cmd_ctrl = pasemi_hwcontrol;
	this->dev_ready = pasemi_device_ready;
	this->read_buf = pasemi_read_buf;
	this->write_buf = pasemi_write_buf;
	this->chip_delay = 1;
	this->ecc.mode = NAND_ECC_SOFT;

	/* Enable the following for a flash based bad block table */
	this->options = NAND_USE_FLASH_BBT | NAND_NO_AUTOINCR;

	/* Scan to find existance of the device */
	if (nand_scan(pasemi_nand_mtd, 1)) {
		err = -ENXIO;
		goto out_lpc;
	}

	if (add_mtd_device(pasemi_nand_mtd)) {
		printk(KERN_ERR "Unable to register MTD device, aborting!\n");
		err = -ENODEV;
		goto out_lpc;
	}

	return 0;

 out_lpc:
	iounmap(lpcctl);
 out_ior:
	iounmap(this->IO_ADDR_R);
 out_mtd:
	kfree(pasemi_nand_mtd);
 out:
	return err;
}

static int __devexit pasemi_nand_remove(struct of_device *ofdev)
{
	struct nand_chip *this;

	if (!pasemi_nand_mtd)
		return 0;

	this = pasemi_nand_mtd->priv;

	/* Release resources, unregister device */
	nand_release(pasemi_nand_mtd);

	iounmap(lpcctl);
	iounmap(this->IO_ADDR_R);

	/* Free the MTD device structure */
	kfree(pasemi_nand_mtd);

	return 0;
}

static struct of_device_id pasemi_nand_match[] =
{
	{
		.compatible   = "pasemi,localbus-nand",
	},
	{},
};

static struct of_platform_driver pasemi_nand_driver =
{
	.name		= (char*)driver_name,
	.match_table	= pasemi_nand_match,
	.probe		= pasemi_nand_probe,
	.remove		= pasemi_nand_remove,
};

static int __init pasemi_nand_init(void)
{
	return of_register_platform_driver(&pasemi_nand_driver);
}
module_init(pasemi_nand_init);

static void __exit pasemi_nand_exit(void)
{
	of_unregister_platform_driver(&pasemi_nand_driver);
}
module_exit(pasemi_nand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Egor Martovetsky <egor@pasemi.com>");
MODULE_DESCRIPTION("NAND flash interface driver for PA Semi PA6T-1682M");
