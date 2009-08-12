/*
 * Driver for the Freescale Semiconductor MC13783 touchscreen.
 *
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2009 Sascha Hauer, Pengutronix
 *
 * Initial development of this code was funded by
 * Phytec Messtechnik GmbH, http://www.phytec.de
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#include <linux/mfd/mc13783-private.h>
#include <linux/platform_device.h>
#include <linux/mfd/mc13783.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/sched.h>
#include <linux/init.h>

#define MC13783_TS_NAME	"mc13783-ts"

struct mc13783_ts_priv {
	struct input_dev *idev;
	struct mc13783 *mc13783;
	struct delayed_work work;
	struct workqueue_struct *workq;
	unsigned int sample[4];
};

#define TS_MIN 1
#define TS_MAX 1000

static void mc13783_ts_handler(int irq, void *data)
{
	struct mc13783_ts_priv *priv = data;
	unsigned int sts;

	mc13783_reg_read(priv->mc13783, MC13783_REG_INTERRUPT_STATUS_0, &sts);
	mc13783_reg_write(priv->mc13783, MC13783_REG_INTERRUPT_STATUS_0,
			sts & MC13783_INT_STAT_TSI);

	if (sts & MC13783_INT_STAT_TSI)
		queue_delayed_work(priv->workq, &priv->work, 0);
}

static void mc13783_ts_report_sample(struct mc13783_ts_priv *priv)
{
	int x, y, press = 0;

	x = (priv->sample[2] >> 2) & 0x3ff;
	y = (priv->sample[3] >> 2) & 0x3ff;

	pr_debug("mc13783_ts: x: %d y: %d\n", x, y);

	if (x > TS_MIN && x < TS_MAX && y > TS_MIN && y < TS_MAX) {
		press = 1;
		input_report_abs(priv->idev, ABS_X, x);
		input_report_abs(priv->idev, ABS_Y, y);

		queue_delayed_work(priv->workq, &priv->work, HZ / 50);
	}

	input_report_key(priv->idev, BTN_TOUCH, press);
	input_sync(priv->idev);
}

static void mc13783_ts_work(struct work_struct *work)
{
	struct mc13783_ts_priv *priv =
		container_of(work, struct mc13783_ts_priv, work.work);
	unsigned int mode = MC13783_ADC_MODE_TS;
	unsigned int channel = 12;
	int ret;

	ret = mc13783_adc_do_conversion
		(priv->mc13783, mode, channel, priv->sample);

	if (!ret)
		mc13783_ts_report_sample(priv);
}

static int mc13783_ts_open(struct input_dev *dev)
{
	struct mc13783_ts_priv *priv = input_get_drvdata(dev);
	int ret;

	ret = mc13783_register_irq(priv->mc13783, MC13783_IRQ_TS,
		mc13783_ts_handler, priv);
	if (ret)
		return ret;

	queue_delayed_work(priv->workq, &priv->work, HZ / 20);

	return 0;
}

static void mc13783_ts_close(struct input_dev *dev)
{
	struct mc13783_ts_priv *priv = input_get_drvdata(dev);

	mc13783_free_irq(priv->mc13783, MC13783_IRQ_TS);
}

static int __devinit mc13783_ts_probe(struct platform_device *pdev)
{
	struct mc13783_ts_priv *priv;
	struct input_dev *idev;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->mc13783 = dev_get_drvdata(pdev->dev.parent);

	idev = input_allocate_device();
	if (!idev) {
		ret = -ENOMEM;
		goto err_input_alloc;
	}

	priv->idev = idev;
	idev->name = MC13783_TS_NAME;
	idev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	idev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	idev->absbit[0] = BIT_MASK(ABS_X) | BIT_MASK(ABS_Y);
	idev->open = mc13783_ts_open;
	idev->close = mc13783_ts_close;
	input_set_abs_params(idev, ABS_X, TS_MIN, TS_MAX, 0, 0);
	input_set_abs_params(idev, ABS_Y, TS_MIN, TS_MAX, 0, 0);

	platform_set_drvdata(pdev, priv);

	INIT_DELAYED_WORK(&priv->work, mc13783_ts_work);

	priv->workq = create_singlethread_workqueue("mc13783_ts");
	if (!priv->workq) {
		ret = -ENOMEM;
		goto err_input_alloc;
	}

	input_set_drvdata(idev, priv);

	ret = input_register_device(priv->idev);
	if (ret) {
		dev_err(&pdev->dev,
				"register input device failed with %d\n", ret);
		goto err_failed_register;
	}

	/* unmask the ts wakeup interrupt */
	mc13783_set_bits(priv->mc13783, MC13783_REG_INTERRUPT_MASK_0,
			MC13783_INT_MASK_TSM, 0);

	mc13783_adc_set_ts_status(priv->mc13783, 1);

	return 0;

err_failed_register:
	input_free_device(priv->idev);
err_input_alloc:
	kfree(priv);

	return ret;
}

static int __devexit mc13783_ts_remove(struct platform_device *pdev)
{
	struct mc13783_ts_priv *priv = platform_get_drvdata(pdev);

	mc13783_adc_set_ts_status(priv->mc13783, 0);

	cancel_delayed_work(&priv->work);
	destroy_workqueue(priv->workq);

	input_unregister_device(priv->idev);

	kfree(priv);

	return 0;
}

static struct platform_driver mc13783_ts_driver = {
	.remove		= __devexit_p(mc13783_ts_remove),
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= MC13783_TS_NAME,
	},
};

static int __init mc13783_ts_init(void)
{
	return platform_driver_probe(&mc13783_ts_driver, &mc13783_ts_probe);
}

static void __exit mc13783_ts_exit(void)
{
	platform_driver_unregister(&mc13783_ts_driver);
}

module_init(mc13783_ts_init);
module_exit(mc13783_ts_exit);

MODULE_DESCRIPTION("MC13783 input touchscreen driver");
MODULE_AUTHOR("Sascha Hauer, <s.hauer@pengutronix.de>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mc13783-touch");
