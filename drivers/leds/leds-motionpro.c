/*
 * LEDs driver for the Motionpro board.
 * 
 * Copyright (C) 2007 Semihalf
 *
 * Author: Jan Wrobel <wrr@semihalf.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *
 * This driver enables control over Motionpro's status and ready LEDs through
 * sysfs. LEDs can be controlled by writing to sysfs files:
 * class/leds/motionpro-(ready|status)led/(brightness|delay_off|delay_on).
 * See Documentation/leds-class.txt for more details
 *
 * Before user issues first control command via sysfs, LED blinking is
 * controlled by the kernel. By default status LED is blinking fast and ready
 * LED is turned off.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/leds.h>

#include <asm/mpc52xx.h>
#include <asm/io.h>

/* Led status */
#define LED_NOT_REGISTERED	0
#define LED_KERNEL_CONTROLLED	1
#define LED_USER_CONTROLLED	2


/* Led control bits */
#define LED_ON	MPC52xx_GPT_OUTPUT_1

static void mpled_set(struct led_classdev *led_cdev,
		      enum led_brightness brightness);

static struct motionpro_led{
	/* Protects the led data */
	spinlock_t led_lock;

	/* Path to led's control register DTS node */
	char *reg_path;

	/* Address to access led's register */
	void __iomem *reg_addr;

	int status;

	/* Blinking timer used when led is controlled by the kernel */
	struct timer_list kernel_mode_timer;

	/*
	 * Delay between blinks when led is controlled by the kernel.
	 * If set to 0 led blinking is off.
	 */
	int kernel_mode_delay;

	struct led_classdev classdev;
}led[] = {
	{
		.reg_path = "/soc5200@f0000000/gpt@660",
		.reg_addr = 0,
		.led_lock = SPIN_LOCK_UNLOCKED,
		.status = LED_NOT_REGISTERED,
		.kernel_mode_delay = HZ / 10,
		.classdev = {
			.name = "motionpro-statusled",
			.brightness_set = mpled_set,
			.default_trigger = "timer",
		},
	},
	{
		.reg_path = "/soc5200@f0000000/gpt@670",
		.reg_addr = 0,
		.led_lock = SPIN_LOCK_UNLOCKED,
		.status = LED_NOT_REGISTERED,
		.kernel_mode_delay = 0,
		.classdev = {
			.name = "motionpro-readyled",
			.brightness_set = mpled_set,
			.default_trigger = "timer",
		}
	}
};

/* Timer event - blinks led before user takes control over it */
static void mpled_timer_toggle(unsigned long ptr)
{
	struct motionpro_led *mled = (struct motionpro_led *) ptr;

	spin_lock_bh(&mled->led_lock);
	if (mled->status == LED_KERNEL_CONTROLLED){
		u32 reg = in_be32(mled->reg_addr);
		reg ^= LED_ON;
		out_be32(mled->reg_addr, reg);
		led->kernel_mode_timer.expires = jiffies +
			led->kernel_mode_delay;
		add_timer(&led->kernel_mode_timer);
	}
	spin_unlock_bh(&mled->led_lock);
}


/*
 * Turn on/off led according to user settings in sysfs.
 * First call to this function disables kernel blinking.
 */
static void mpled_set(struct led_classdev *led_cdev,
		      enum led_brightness brightness)
{
	struct motionpro_led *mled;
	u32 reg;

	mled = container_of(led_cdev, struct motionpro_led, classdev);

	spin_lock_bh(&mled->led_lock);
	mled->status = LED_USER_CONTROLLED;

	reg = in_be32(mled->reg_addr);
	if (brightness)
		reg |= LED_ON;
	else
		reg &= ~LED_ON;
	out_be32(mled->reg_addr, reg);

	spin_unlock_bh(&mled->led_lock);
}

static void mpled_init_led(void __iomem *reg_addr)
{
	u32 reg = in_be32(reg_addr);
	reg |= MPC52xx_GPT_ENABLE_OUTPUT;
	reg &= ~LED_ON;
	out_be32(reg_addr, reg);
}

static void mpled_clean(void)
{
	int i;
	for (i = 0; i < sizeof(led) / sizeof(struct motionpro_led); i++){
		if (led[i].status != LED_NOT_REGISTERED){
			spin_lock_bh(&led[i].led_lock);
			led[i].status = LED_NOT_REGISTERED;
			spin_unlock_bh(&led[i].led_lock);
			led_classdev_unregister(&led[i].classdev);
		}
		if (led[i].reg_addr){
			iounmap(led[i].reg_addr);
			led[i].reg_addr = 0;
		}
	}
}

static int __init mpled_init(void)
{
	int i, error;

	for (i = 0; i < sizeof(led) / sizeof(struct motionpro_led); i++){
		led[i].reg_addr = mpc52xx_find_and_map_path(led[i].reg_path);
		if (!led[i].reg_addr){
			printk(KERN_ERR __FILE__ ": "
			       "Error while mapping GPIO register for LED %s\n",
			       led[i].classdev.name);
			error = -EIO;
			goto err;
		}

		mpled_init_led(led[i].reg_addr);
		led[i].status = LED_KERNEL_CONTROLLED;
		if (led[i].kernel_mode_delay){
			init_timer(&led[i].kernel_mode_timer);
			led[i].kernel_mode_timer.function = mpled_timer_toggle;
			led[i].kernel_mode_timer.data = (unsigned long)&led[i];
			led[i].kernel_mode_timer.expires =
				jiffies + led[i].kernel_mode_delay;
			add_timer(&led[i].kernel_mode_timer);
		}

		if ((error = led_classdev_register(NULL, &led[i].classdev)) < 0){
			printk(KERN_ERR __FILE__ ": "
			       "Error while registering class device for LED "
			       "%s\n",
			       led[i].classdev.name);
			goto err;
		}
	}

	printk("Motionpro LEDs driver initialized\n");
	return 0;
err:
	mpled_clean();
	return error;
}

static void __exit mpled_exit(void)
{
	mpled_clean();
}

module_init(mpled_init);
module_exit(mpled_exit);

MODULE_LICENSE("GPL")
MODULE_DESCRIPTION("LEDs support for Motionpro");
MODULE_AUTHOR("Jan Wrobel <wrr@semihalf.com>");
