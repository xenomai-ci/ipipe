/*
 *  drivers/mtd/nand/alpr_nand.c
 *
 *  Overview:
 *   This is a device driver for the NAND flash devices found on the
 *   Prodrive ALPR board
 *
 *   Heiko Schocher <hs@denx.de>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <asm/ibm44x.h>
#include <platforms/4xx/alpr.h>

#if 0
#define CPLD_REG        u8
#else
#define CPLD_REG        u16
#endif

struct alpr_ndfc_regs {
	CPLD_REG cmd[4];
	CPLD_REG addr_wait;
	CPLD_REG term;
	CPLD_REG dummy;
	u8    dum2[2];
	CPLD_REG data;
};

static struct mtd_info *alpr_nand0_mtd;
static struct mtd_info *alpr_nand1_mtd;
static u8 hwctl;
static struct alpr_ndfc_regs *alpr_ndfc;
static int      alpr_chip = 0;

static void alpr_write_byte(struct mtd_info *mtd, u_char byte);

#define NAND0_NUM_PARTITIONS 4
static struct mtd_partition nand0_partition_info[] = {
	{
	 	.name = "fpga",
	 	.offset = 0x0,
	 	.size = 2 << 20,
	 },
	{
	 	.name = "kernel1",
	 	.offset = MTDPART_OFS_APPEND,
	 	.size = 2 << 20,
	 },
	{
	 	.name = "kernel2",
	 	.offset = MTDPART_OFS_APPEND,
	 	.size = 2 << 20,
	 },
	{
	 	.name = "fs",
	 	.offset = MTDPART_OFS_APPEND,
	 	.size = MTDPART_SIZ_FULL,
	 },
};

#define NAND1_NUM_PARTITIONS 1
static struct mtd_partition nand1_partition_info[] = {
	{
		.name = "filesystem",
		.offset = 0x0,
		.size = 0x1000000,
	}
};
static int nr_partitions;

/*
 * The 440EP has a NAND Flash Controller (NDFC) that handles all accesses to
 * the NAND devices.  The NDFC has command, address and data registers that
 * when accessed will set up the NAND flash pins appropriately.  We'll use the
 * hwcontrol function to save the configuration in a global variable.
 * We can then use this information in the read and write functions to
 * determine which NDFC register to access. For the NCE commands, we'll just
 * set or clear the Bank Enable bit in the NDFC Bank Config registers.
 *
 * There are 2 NAND devices on the board, a Samsung K9F1208U0A (64 MB) and a
 * Samsung K9K2G08U0M (256 MB).
 */
static void
alpr_hwcontrol(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
/*printk("%s: cmd:%x ctrl:%x change:%x CLE:%x\n", __FUNCTION__, cmd, ctrl, NAND_CTRL_CHANGE, NAND_CLE);*/
	if (ctrl & NAND_CTRL_CHANGE)
	{
		if (ctrl & NAND_NCE) {
		/*	alpr_write(0x00, &(alpr_ndfc->term));*/
		}

		if (ctrl & NAND_CLE)
			hwctl |= 0x1;
		else
			hwctl &= ~0x1;

		if (ctrl & NAND_ALE)
			hwctl |= 0x2;
		else
			hwctl &= ~0x2;
	}
	if (cmd != NAND_CMD_NONE)
		alpr_write_byte(mtd, cmd);
	else
		hwctl = 0;
}

static void
alpr_nand0_enable(void)
{
	alpr_chip = 0;
}

static void
alpr_nand1_enable(void)
{
	alpr_chip = 1;
}

static void alpr_write (u8 val, volatile void __iomem *addr)
{
/*printk("%s addr:%p val: %x\n", __FUNCTION__, addr, val);*/
	__raw_writew (val, addr);
}

static u16 alpr_read (volatile void __iomem *addr)
{
	u16 val;
	val = __raw_readw (addr);
/*printk("%s addr:%p val: %x\n", __FUNCTION__, addr, val);*/
	return val;
}

static void
alpr_write_byte(struct mtd_info *mtd, u_char byte)
{
/*printk("%s: chip:%d byte:%x\n", __FUNCTION__, alpr_chip, byte);*/
	if (hwctl & 0x1)
		alpr_write(byte, &(alpr_ndfc->cmd[alpr_chip]));
	else if (hwctl & 0x2)
		alpr_write(byte, &(alpr_ndfc->addr_wait));
	else
		alpr_write(byte, &(alpr_ndfc->data));
}

static void
alpr_nand0_write_byte(struct mtd_info *mtd, u_char byte)
{
	alpr_nand0_enable();
	alpr_write_byte(mtd, byte);
}

static void
alpr_nand1_write_byte(struct mtd_info *mtd, u_char byte)
{
	alpr_nand1_enable();
	alpr_write_byte(mtd,byte);
}

static u_char
alpr_read_byte(struct mtd_info *mtd)
{
	u_char retval;
	if (hwctl & 0x1)
		retval = alpr_read(&(alpr_ndfc->cmd[alpr_chip]));
	else if (hwctl & 0x2)
		retval = alpr_read(&(alpr_ndfc->addr_wait));
	else
		retval = alpr_read(&(alpr_ndfc->data));
	return retval;
}

static u_char
alpr_nand0_read_byte(struct mtd_info *mtd)
{
	alpr_nand0_enable();
	return alpr_read_byte(mtd);
}

static u_char
alpr_nand1_read_byte(struct mtd_info *mtd)
{
	alpr_nand1_enable();
	return alpr_read_byte(mtd);
}

static void
alpr_nand_write_buf(struct mtd_info *mtd, const u_char * buf, int len)
{
	int i;
	for (i = 0; i < len; i++) {
		if (hwctl & 0x1)
			alpr_write(buf[i], &(alpr_ndfc->cmd[alpr_chip]));
		else if (hwctl & 0x2)
			alpr_write(buf[i], &(alpr_ndfc->addr_wait));
		else
			alpr_write(buf[i], &(alpr_ndfc->data));
	}
}

static void
alpr_nand0_write_buf(struct mtd_info *mtd, const u_char * buf, int len)
{
	alpr_nand0_enable();
	alpr_nand_write_buf(mtd, buf, len);
}

static void
alpr_nand1_write_buf(struct mtd_info *mtd, const u_char * buf, int len)
{
	alpr_nand1_enable();
	alpr_nand_write_buf(mtd, buf, len);
}

static void
alpr_nand_read_buf(struct mtd_info *mtd, u_char * buf, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (hwctl & 0x1)
			buf[i] = alpr_read(&(alpr_ndfc->cmd[alpr_chip]));
		else if (hwctl & 0x2)
			buf[i] = alpr_read(&(alpr_ndfc->addr_wait));
		else
			buf[i] = alpr_read(&(alpr_ndfc->data));
	}
}

static void
alpr_nand0_read_buf(struct mtd_info *mtd, u_char * buf, int len)
{
	alpr_nand0_enable();
	alpr_nand_read_buf(mtd, buf, len);
}

static void
alpr_nand1_read_buf(struct mtd_info *mtd, u_char * buf, int len)
{
	alpr_nand1_enable();
	alpr_nand_read_buf(mtd, buf, len);
}

static int
alpr_nand_verify_buf(struct mtd_info *mtd, const u_char * buf, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (hwctl & 0x1) {
			if (buf[i] != alpr_read(&(alpr_ndfc->cmd[alpr_chip])))
				return i;
		} else if (hwctl & 0x2) {
			if (buf[i] != alpr_read(&(alpr_ndfc->addr_wait)))
				return i;
		} else {
			if (buf[i] != alpr_read(&(alpr_ndfc->data)))
				return i;
		}

	}

	return 0;
}

static int
alpr_nand0_verify_buf(struct mtd_info *mtd, const u_char * buf, int len)
{
	alpr_nand0_enable();
	return alpr_nand_verify_buf(mtd, buf, len);
}

static int
alpr_nand1_verify_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	alpr_nand1_enable();
	return alpr_nand_verify_buf(mtd, buf, len);
}

static int
alpr_dev_ready(struct mtd_info *mtd)
{
	volatile u_char val;

/*	val = alpr_read (&(alpr_ndfc->addr_wait));*/
	return 1;
}

void alpr_select_chip (struct mtd_info *mtd, int chip)
{
	alpr_chip = chip;
}

#ifdef CONFIG_MTD_PARTITIONS
const char *part_probes[] = { "cmdlinepart", NULL };
#endif

int __init
alpr_init(void)
{
	struct mtd_partition* alpr_partition_info;
	struct nand_chip *this;
	int err = 0;

	hwctl = 0;

	alpr_nand0_mtd = kmalloc(sizeof(struct mtd_info) +
				   sizeof(struct nand_chip),
				   GFP_KERNEL);

	alpr_nand1_mtd = kmalloc(sizeof (struct mtd_info) +
				   sizeof (struct nand_chip),
				   GFP_KERNEL);
	if (!alpr_nand1_mtd) {
		printk("Unable to allocate NAND 1 MTD device structure.\n");
		err = -ENOMEM;
		goto out_mtd0;
	}

	alpr_ndfc = ioremap64(ALPR_NAND_FLASH_REG_ADDR,
			      0x100);
printk ("%s: alpr_ndfc: phy:%x %p size:%x\n", __FUNCTION__, ALPR_NAND_FLASH_REG_ADDR, alpr_ndfc, sizeof(struct alpr_ndfc_regs));
	if (!alpr_ndfc) {
		printk("Ioremap to access NDFC Registers failed \n");
		err = -EIO;
		goto out_mtd1;
	}

	/* Initialize structures */
	memset((char *) alpr_nand0_mtd, 0,
	       sizeof (struct mtd_info) + sizeof (struct nand_chip));

	memset((char *) alpr_nand1_mtd, 0,
	       sizeof (struct mtd_info) + sizeof (struct nand_chip));

	/* Get pointer to private data */
	this = (struct nand_chip *) (&alpr_nand0_mtd[1]);
	/* Link the private data with the MTD structure */
	alpr_nand0_mtd->priv = this;

	/* Set address of NAND IO lines (Using Linear Data Access Region) */
	this->IO_ADDR_R = (void __iomem *) ((ulong) alpr_ndfc + 0x10);
	this->IO_ADDR_W = (void __iomem *) ((ulong) alpr_ndfc + 0x10);
	/* Reference hardware control function */
	this->cmd_ctrl  = alpr_hwcontrol;
	/* Set command delay time */
	this->select_chip = alpr_select_chip;
	this->read_byte  = alpr_nand0_read_byte;
	this->write_buf  = alpr_nand0_write_buf;
	this->read_buf   = alpr_nand0_read_buf;
	this->verify_buf = alpr_nand0_verify_buf;
	this->dev_ready  = alpr_dev_ready;
	this->ecc.mode = NAND_ECC_SOFT;

	/* Scan to find existance of the device */
	if (nand_scan(alpr_nand0_mtd, 1)) {
		err = -ENXIO;
		goto out_ior;
	}

	/* Register the partitions */
	alpr_nand0_mtd->name = "alpr-nand0";
	nr_partitions = parse_mtd_partitions(alpr_nand0_mtd, part_probes,
						&alpr_partition_info, 0);
	if (nr_partitions <= 0) {
		nr_partitions = NAND0_NUM_PARTITIONS;
		alpr_partition_info = nand0_partition_info;
	}

	add_mtd_partitions(alpr_nand0_mtd, alpr_partition_info, nr_partitions);

	/* Get pointer to private data */
	this = (struct nand_chip *) (&alpr_nand1_mtd[1]);
	/* Link the private data with the MTD structure */
	alpr_nand1_mtd->priv = this;

	/* Set address of NAND IO lines (Using Linear Data Access Region) */
	this->IO_ADDR_R = (void __iomem *) ((ulong) alpr_ndfc + 0x10);
	this->IO_ADDR_W = (void __iomem *) ((ulong) alpr_ndfc + 0x10);
	/* Reference hardware control function */
	this->cmd_ctrl  = alpr_hwcontrol;
	/* Set command delay time */
	this->select_chip = alpr_select_chip;
	this->read_byte  = alpr_nand1_read_byte;
	this->write_buf  = alpr_nand1_write_buf;
	this->read_buf   = alpr_nand1_read_buf;
	this->verify_buf = alpr_nand1_verify_buf;
	this->dev_ready  = alpr_dev_ready;
	this->ecc.mode = NAND_ECC_SOFT;

#if 1
	/* Scan to find existance of the device */
	if (nand_scan(alpr_nand1_mtd, 1)) {
		err = 0;
		goto out_mtd1;
	}

	/* Register the partitions */
	alpr_nand1_mtd->name = "alpr-nand1";
	nr_partitions = parse_mtd_partitions(alpr_nand1_mtd, part_probes,
						&alpr_partition_info, 0);
	if (nr_partitions <= 0) {
		nr_partitions = NAND1_NUM_PARTITIONS;
		alpr_partition_info = nand1_partition_info;
	}

	add_mtd_partitions(alpr_nand1_mtd, alpr_partition_info, nr_partitions);
#endif

	goto out;

out_ior:
	iounmap((void *)alpr_ndfc);
out_mtd0:
	kfree(alpr_nand0_mtd);
out_mtd1:
	kfree(alpr_nand1_mtd);
out:
	return err;
}

static void __exit
alpr_cleanup(void)
{
	/* Unregister partitions */
	del_mtd_partitions(alpr_nand0_mtd);
	del_mtd_partitions(alpr_nand1_mtd);

	/* Release resources, unregister device */
	del_mtd_device(alpr_nand0_mtd);
	del_mtd_device(alpr_nand1_mtd);

	nand_release(alpr_nand0_mtd);
	/* unmap physical address */
	iounmap((void *) alpr_ndfc);

	/* Free the MTD device structure */
	kfree(alpr_nand0_mtd);
	kfree(alpr_nand1_mtd);
}

module_init(alpr_init);
module_exit(alpr_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Heiko Schocher <hs@denx.de>");
MODULE_DESCRIPTION
    ("Board-specific glue layer for NAND flash on Prodrive ALPR board");
