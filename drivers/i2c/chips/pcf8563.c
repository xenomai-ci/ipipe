/*
 *  linux/drivers/i2c/chips/pcf8563.c
 *
 *  Copyright (C) 2002-2004 Stefan Eletzhofer
 *
 *	based on linux/drivers/acron/char/pcf8583.c
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Driver for system3's PHILIPS PCF 8563 chip
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/rtc.h>		/* get the user-level API */
#include <linux/init.h>
#include <linux/list.h>
#include <asm/time.h>
#include <asm/machdep.h>

#include "rtc8564.h"

#ifdef DEBUG
# define _DBG(x, fmt, args...) do{ if (debug>=x) printk(KERN_DEBUG"%s: " fmt "\n", __FUNCTION__, ##args); } while(0);
#else
# define _DBG(x, fmt, args...) do { } while(0);
#endif

#define _DBGRTCTM(x, rtctm) if (debug>x) printk("%s: secs=%d, mins=%d, hours=%d, mday=%d, " \
			"mon=%d, year=%d, wday=%d\n", __FUNCTION__, \
			(rtctm).tm_sec, (rtctm).tm_min, (rtctm).tm_hour, (rtctm).tm_mday, \
			(rtctm).tm_mon, (rtctm).tm_year, (rtctm).tm_wday);

struct pcf8563_data {
	struct i2c_client client;
	struct list_head list;
	u16 ctrl;
};

/*
 * Internal variables
 */
static LIST_HEAD(pcf8563_clients);

static 	struct i2c_client *myclient = NULL;

static inline u8 _pcf8563_ctrl1(struct i2c_client *client)
{
	struct pcf8563_data *data = i2c_get_clientdata(client);
	return data->ctrl & 0xff;
}
static inline u8 _pcf8563_ctrl2(struct i2c_client *client)
{
	struct pcf8563_data *data = i2c_get_clientdata(client);
	return (data->ctrl & 0xff00) >> 8;
}

#define CTRL1(c) _pcf8563_ctrl1(c)
#define CTRL2(c) _pcf8563_ctrl2(c)

#define BCD_TO_BIN(val) (((val)&15) + ((val)>>4)*10)
#define BIN_TO_BCD(val) ((((val)/10)<<4) + (val)%10)

static int debug = 0;
module_param(debug, int, S_IRUGO | S_IWUSR);

static struct i2c_driver pcf8563_driver;

static int pcf8563_read_mem(struct i2c_client *client, struct mem *mem);
static int pcf8563_write_mem(struct i2c_client *client, struct mem *mem);
static unsigned long    pcf8563_get_rtc_time(void);
static int              pcf8563_set_rtc_time(unsigned long now);

/* save/restore old machdep pointers */
int             (*save_set_rtc_time)(unsigned long);
unsigned long   (*save_get_rtc_time)(void);

static int pcf8563_read(struct i2c_client *client, unsigned char adr,
			unsigned char *buf, unsigned char len)
{
	int ret = -EIO;
	unsigned char addr[1] = { adr };
	struct i2c_msg msgs[2] = {
		{client->addr, 0, 1, addr},
		{client->addr, I2C_M_RD, len, buf}
	};

	_DBG(1, "client=%p, adr=%d, buf=%p, len=%d", client, adr, buf, len);

	if (!buf) {
		ret = -EINVAL;
		goto done;
	}

	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret == 2) {
		ret = 0;
	}

done:
	return ret;
}

static int pcf8563_write(struct i2c_client *client, unsigned char adr,
			 unsigned char *data, unsigned char len)
{
	int ret = 0;
	unsigned char _data[16];
	struct i2c_msg wr;
	int i;

	if (!data || len > 15) {
		ret = -EINVAL;
		goto done;
	}

	_DBG(1, "client=%p, adr=%d, buf=%p, len=%d", client, adr, data, len);

	_data[0] = adr;
	for (i = 0; i < len; i++) {
		_data[i + 1] = data[i];
		_DBG(5, "data[%d] = 0x%02x (%d)", i, data[i], data[i]);
	}

	wr.addr = client->addr;
	wr.flags = 0;
	wr.len = len + 1;
	wr.buf = _data;

	ret = i2c_transfer(client->adapter, &wr, 1);
	if (ret == 1) {
		ret = 0;
	}

done:
	return ret;
}

static void pcf8563_set_system_time (void)
{
        unsigned long now, flags;
        extern time_t last_rtc_update;
	extern seqlock_t xtime_lock;

        /* Switching kernel RTC pointers */
        _DBG (2,"RTC switching kernel pointers\n");

        save_set_rtc_time   = ppc_md.set_rtc_time;
        save_get_rtc_time   = ppc_md.get_rtc_time;

        ppc_md.set_rtc_time = pcf8563_set_rtc_time;
        ppc_md.get_rtc_time = pcf8563_get_rtc_time;

        /*
         * Set system time
         * Code copied from arch/ppc/kernel/time.c
         */
        write_seqlock_irqsave(&xtime_lock, flags);

        now = pcf8563_get_rtc_time();
        _DBG (2,"Set System Time from RTC Time: %lu\n", now);
        xtime.tv_nsec = 0;
        xtime.tv_sec  = now;

        last_rtc_update = now - 658;

        time_adjust   = 0;              /* stop active adjtime() */
        time_status  |= STA_UNSYNC;
        time_state    = TIME_ERROR;
        time_maxerror = NTP_PHASE_LIMIT;
        time_esterror = NTP_PHASE_LIMIT;

        write_sequnlock_irqrestore(&xtime_lock, flags);

#if 0
        /*
         * Check for low voltage
         */
        if (rtc_rd (0x02) & 0x80) {
                printk (KERN_CRIT
                        "PCF8563 RTC Low Voltage - date/time not reliable\n");
        }

        /*
         * Reset any error conditions, alarms, etc.
         */
        pcf8563_reset_rtc ();
#endif
}

static int pcf8563_attach(struct i2c_adapter *adap, int addr, int kind)
{
	int ret;
	struct i2c_client *new_client;
	struct pcf8563_data *d;
	unsigned char data[10];
	unsigned char ad[1] = { 0 };
	struct i2c_msg ctrl_wr[1] = {
		{addr, 0, 2, data}
	};
	struct i2c_msg ctrl_rd[2] = {
		{addr, 0, 1, ad},
		{addr, I2C_M_RD, 2, data}
	};

	d = kmalloc(sizeof(struct pcf8563_data), GFP_KERNEL);
	if (!d) {
		ret = -ENOMEM;
		goto done;
	}
	memset(d, 0, sizeof(struct pcf8563_data));
	INIT_LIST_HEAD(&d->list);

	new_client = &d->client;

	strlcpy(new_client->name, "RTC8563", I2C_NAME_SIZE);
	i2c_set_clientdata(new_client, d);
	new_client->addr = addr;
	new_client->adapter = adap;
	new_client->driver = &pcf8563_driver;

	_DBG(1, "client=%p", new_client);

	/* init ctrl1 reg */
	data[0] = 0;
	data[1] = 0;
	ret = i2c_transfer(new_client->adapter, ctrl_wr, 1);
	if (ret != 1) {
		printk(KERN_INFO "pcf8563: cant init ctrl1\n");
		ret = -ENODEV;
		goto done;
	}
	/* read back ctrl1 and ctrl2 */
	ret = i2c_transfer(new_client->adapter, ctrl_rd, 2);
	if (ret != 2) {
		printk(KERN_INFO "pcf8563: cant read ctrl\n");
		ret = -ENODEV;
		goto done;
	}
	d->ctrl = data[0] | (data[1] << 8);

	_DBG(1, "RTC8564_REG_CTRL1=%02x, RTC8564_REG_CTRL2=%02x",
	     data[0], data[1]);

	ret = i2c_attach_client(new_client);

	/* Add client to local list */
	list_add(&d->list, &pcf8563_clients);

done:
	if (ret) {
		kfree(d);
	} else {
		myclient = new_client;
		pcf8563_set_system_time();
	}
	return ret;
}

static int pcf8563_probe(struct i2c_adapter *adap)
{
        /*
         * Probing seems to confuse the RTC on PM826 and CPU86 and UC100.
         * Not sure if it's true for other boards though - thus the
         * conditional.
         */
#if defined(CONFIG_PM82X)
	pcf8563_attach(adap, 0x51, 0);
	return 0;
#else
	return i2c_probe(adap, &addr_data, pcf8563_attach);
#endif
}

static int pcf8563_detach(struct i2c_client *client)
{
	struct pcf8563_data *data = i2c_get_clientdata(client);

	i2c_detach_client(client);
	kfree(i2c_get_clientdata(client));
	list_del(&data->list);
	return 0;
}

static int pcf8563_get_datetime(struct i2c_client *client, struct rtc_time *dt)
{
	int ret = -EIO;
	unsigned char buf[15];

	_DBG(1, "client=%p, dt=%p", client, dt);

	if (!dt)
		return -EINVAL;

	memset(buf, 0, sizeof(buf));

	ret = pcf8563_read(client, 0, buf, 15);
	if (ret)
		return ret;

	dt->tm_year = 1900 + BCD_TO_BIN(buf[RTC8564_REG_YEAR]);
	if (buf[RTC8564_REG_MON_CENT] & 0x80)
		dt->tm_year += 100;
	dt->tm_mday = BCD_TO_BIN(buf[RTC8564_REG_DAY] & 0x3f);
	dt->tm_wday = BCD_TO_BIN(buf[RTC8564_REG_WDAY] & 7);
	dt->tm_mon = BCD_TO_BIN(buf[RTC8564_REG_MON_CENT] & 0x1f);

	dt->tm_sec = BCD_TO_BIN(buf[RTC8564_REG_SEC] & 0x7f);
//	dt->vl = (buf[RTC8564_REG_SEC] & 0x80) == 0x80;
	dt->tm_min = BCD_TO_BIN(buf[RTC8564_REG_MIN] & 0x7f);
	dt->tm_hour = BCD_TO_BIN(buf[RTC8564_REG_HR] & 0x3f);

	_DBGRTCTM(2, *dt);
	return 0;
}

static int
pcf8563_set_datetime(struct i2c_client *client, struct rtc_time *dt, int datetoo)
{
	int ret, len = 5;
	unsigned char buf[15];
	unsigned char val;

	_DBG(1, "client=%p, dt=%p", client, dt);

	if (!dt)
		return -EINVAL;

#if 0
	_DBGRTCTM(2, *dt);
#endif

	buf[RTC8564_REG_CTRL1] = CTRL1(client) | RTC8564_CTRL1_STOP;
	buf[RTC8564_REG_CTRL2] = CTRL2(client);
	buf[RTC8564_REG_SEC] = BIN_TO_BCD(dt->tm_sec);
	buf[RTC8564_REG_MIN] = BIN_TO_BCD(dt->tm_min);
	buf[RTC8564_REG_HR] = BIN_TO_BCD(dt->tm_hour);

	if (datetoo) {
		len += 5;
		buf[RTC8564_REG_DAY] = BIN_TO_BCD(dt->tm_mday);
		buf[RTC8564_REG_WDAY] = BIN_TO_BCD(dt->tm_wday);
		buf[RTC8564_REG_MON_CENT] = BIN_TO_BCD(dt->tm_mon) & 0x1f;
		if (dt->tm_year >= 2000) {
			val = dt->tm_year - 2000;
			buf[RTC8564_REG_MON_CENT] |= (1 << 7);
		} else {
			val = dt->tm_year - 1900;
		}
		buf[RTC8564_REG_YEAR] = BIN_TO_BCD(val);
	}

	ret = pcf8563_write(client, 0, buf, len);
	if (ret) {
		_DBG(1, "error writing data! %d", ret);
	}

	buf[RTC8564_REG_CTRL1] = CTRL1(client);
	ret = pcf8563_write(client, 0, buf, 1);
	if (ret) {
		_DBG(1, "error writing data! %d", ret);
	}

	return ret;
}

static int pcf8563_get_ctrl(struct i2c_client *client, unsigned int *ctrl)
{
	struct pcf8563_data *data = i2c_get_clientdata(client);

	if (!ctrl)
		return -1;

	*ctrl = data->ctrl;
	return 0;
}

static int pcf8563_set_ctrl(struct i2c_client *client, unsigned int *ctrl)
{
	struct pcf8563_data *data = i2c_get_clientdata(client);
	unsigned char buf[2];

	if (!ctrl)
		return -1;

	buf[0] = *ctrl & 0xff;
	buf[1] = (*ctrl & 0xff00) >> 8;
	data->ctrl = *ctrl;

	return pcf8563_write(client, 0, buf, 2);
}

static int pcf8563_read_mem(struct i2c_client *client, struct mem *mem)
{

	if (!mem)
		return -EINVAL;

	return pcf8563_read(client, mem->loc, mem->data, mem->nr);
}

static int pcf8563_write_mem(struct i2c_client *client, struct mem *mem)
{

	if (!mem)
		return -EINVAL;

	return pcf8563_write(client, mem->loc, mem->data, mem->nr);
}

static int
pcf8563_command(struct i2c_client *client, unsigned int cmd, void *arg)
{

	_DBG(1, "cmd=%d", cmd);

	switch (cmd) {
	case RTC_GETDATETIME:
		return pcf8563_get_datetime(client, arg);

	case RTC_SETTIME:
		return pcf8563_set_datetime(client, arg, 0);

	case RTC_SETDATETIME:
		return pcf8563_set_datetime(client, arg, 1);

	case RTC_GETCTRL:
		return pcf8563_get_ctrl(client, arg);

	case RTC_SETCTRL:
		return pcf8563_set_ctrl(client, arg);

	case MEM_READ:
		return pcf8563_read_mem(client, arg);

	case MEM_WRITE:
		return pcf8563_write_mem(client, arg);

	default:
		return -EINVAL;
	}
}

/*
 * Public API for access to specific device. Useful for low-level
 * RTC access from kernel code.
 */
int pcf8563_do_command(int bus, int cmd, void *arg)
{
	struct list_head *walk;
	struct list_head *tmp;
	struct pcf8563_data *data;

	list_for_each_safe(walk, tmp, &pcf8563_clients) {
		data = list_entry(walk, struct pcf8563_data, list);
		if (data->client.adapter->nr == bus) {
			return pcf8563_command(&data->client, cmd, arg);
		}
	}

	return -ENODEV;
}

extern spinlock_t rtc_lock;
/***************************************************************************
 *
 * get RTC time:
 */
static unsigned long pcf8563_get_rtc_time(void)
{
#if 0
	struct rtc_time tm;

	pcf8563_get_datetime(myclient, &tm);

        return (mktime (tm.tm_year,
			tm.tm_mon,
			tm.tm_mday,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec
 			) );
#else
	struct rtc_time tm;
	int result;

	spin_lock(&rtc_lock);
	result = pcf8563_do_command(0, RTC_GETDATETIME, &tm);
	spin_unlock(&rtc_lock);

	if (result == 0)
		result = mktime(tm.tm_year, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

	return result;
#endif
}

/***************************************************************************
 *
 * set RTC time:
 */
static int pcf8563_set_rtc_time(unsigned long now)
{
        struct rtc_time tm;
        unsigned char century, year, mon, wday, mday, hour, min, sec;

        to_tm (now, &tm);

        _DBG (2, "Set RTC [dec] year=%d mon=%d day=%d hour=%d min=%d sec=%d\n",
                tm.tm_year, tm.tm_mon, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);

        century = (tm.tm_year >= 2000) ? 0x80 : 0;
        year = BIN_TO_BCD (tm.tm_year % 100);
        mon  = BIN_TO_BCD (tm.tm_mon) | century;
        wday = BIN_TO_BCD (tm.tm_wday);
        mday = BIN_TO_BCD (tm.tm_mday);
        hour = BIN_TO_BCD (tm.tm_hour);
        min  = BIN_TO_BCD (tm.tm_min);
        sec  = BIN_TO_BCD (tm.tm_sec);

        _DBG (2, "Set RTC [bcd] year=%X mon=%X day=%X "
                "hour=%X min=%X sec=%X wday=%X\n",
                year, mon, mday, hour, min, sec, wday);

	pcf8563_set_datetime(myclient, &tm, 1);

        return (0);
}

static struct i2c_driver pcf8563_driver = {
	.driver = {
		.name	= "PCF8563",
	},
	.id		= I2C_DRIVERID_RTC8564,
	.attach_adapter = pcf8563_probe,
	.detach_client	= pcf8563_detach,
	.command	= pcf8563_command
};

static __init int pcf8563_init(void)
{
	return i2c_add_driver(&pcf8563_driver);
}

static __exit void pcf8563_exit(void)
{
        ppc_md.set_rtc_time = save_set_rtc_time;
        ppc_md.get_rtc_time = save_get_rtc_time;

	i2c_del_driver(&pcf8563_driver);
}

MODULE_AUTHOR("Stefan Eletzhofer <Stefan.Eletzhofer@eletztrick.de>");
MODULE_DESCRIPTION("EPSON RTC8563 Driver");
MODULE_LICENSE("GPL");

module_init(pcf8563_init);
module_exit(pcf8563_exit);
