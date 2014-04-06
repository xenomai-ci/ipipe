/*
 * linux/arch/arm/mach-at91/at91_ipipe.c
 *
 * Copyright (C) 2007 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
 *
 * Adaptation to AT91SAM926x:
 * Copyright (C) 2007 Gregory CLEMENT, Adeneo
 *
 * Adaptation to AT91SAM9G45:
 * Copyright (C) 2011 Gregory CLEMENT, Free Electrons
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/clockchips.h>
#include <linux/clk.h>
#include <linux/stringify.h>
#include <linux/ipipe.h>
#include <linux/atmel_tc.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/ipipe_tickdev.h>
#include "at91_ipipe.h"

#define TCNXCNS(timer,v) ((v) << ((timer)<<1))
#define AT91_TC_REG_MASK (0xffff)

#define at91_tc_read(reg) \
	__raw_readl(at91_tc_base + ATMEL_TC_REG(CONFIG_IPIPE_AT91_TC % 3, reg))

#define at91_tc_write(reg, value) \
	__raw_writel(value, at91_tc_base + ATMEL_TC_REG(CONFIG_IPIPE_AT91_TC % 3, reg))

#define read_CV() at91_tc_read(CV)
#define read_RC() at91_tc_read(RC)
#define write_RC(value) at91_tc_write(RC, value)

static void __iomem *at91_tc_base;
static unsigned max_delta_ticks;
static unsigned at91_ipipe_divisor;
static int tc_timer_clock;

/*
 * Reprogram the timer
 */
static int at91_tc_set(unsigned long evt, void *timer);

/*
 * IRQ handler for the timer.
 */
static void at91_tc_ack(void)
{
	at91_tc_read(SR);
}

static void at91_tc_request(struct ipipe_timer *timer, int steal)
{
	/* Enable CPCS interrupt. */
	at91_tc_write(IER, ATMEL_TC_CPCS);
}

static void at91_tc_release(struct ipipe_timer *timer)
{
	/* Disable all interrupts. */
	at91_tc_write(IDR, ~0ul);
}

static struct ipipe_timer at91_itimer = {
	.request        = at91_tc_request,
	.set            = at91_tc_set,
	.ack            = at91_tc_ack,
	.release        = at91_tc_release,

	.name		= "at91_tc" __stringify(CONFIG_IPIPE_AT91_TC),
	.rating		= 250,
};

static int at91_tc_set(unsigned long evt, void *timer)
{
	unsigned short next_tick;

	if (evt > max_delta_ticks)
		evt = max_delta_ticks;

	__ipipe_tsc_update();

	next_tick = read_CV() + evt;
	write_RC(next_tick);
	if (evt >= AT91_TC_REG_MASK / 2
	    || (short)(next_tick - read_CV()) > 0)
		return 0;

	at91_itimer.min_delay_ticks = evt;
	return -ETIME;
}

static struct __ipipe_tscinfo tsc_info = {
	.type = IPIPE_TSC_TYPE_FREERUNNING,
	.u = {
		{
			.mask = AT91_TC_REG_MASK,
		},
	},
};

void at91_ipipe_early_init(struct clock_event_device *host_timer)
{
	unsigned master_freq, divided_freq = 0;
	unsigned long long wrap_ns;

	master_freq = clk_get_rate(clk_get(NULL, "mck"));
	/* Find the first frequency above 1 MHz */
	for (tc_timer_clock = ARRAY_SIZE(atmel_tc_divisors) - 1;
	     tc_timer_clock >= 0; tc_timer_clock--) {
		at91_ipipe_divisor = atmel_tc_divisors[tc_timer_clock];
		divided_freq = (at91_ipipe_divisor
				? master_freq / at91_ipipe_divisor : AT91_SLOW_CLOCK);
		if (divided_freq > 1000000)
			break;
	}

	wrap_ns = (unsigned long long) (AT91_TC_REG_MASK + 1) * NSEC_PER_SEC;
	do_div(wrap_ns, divided_freq);

	if (divided_freq < 1000000)
		printk(KERN_INFO "AT91 I-pipe warning: could not find a"
		       " frequency greater than 1MHz\n");

	printk(KERN_INFO "AT91 I-pipe timer: using TC%d, div: %u, "
		"freq: %u.%06u MHz, wrap: %u.%06u ms\n", 
		CONFIG_IPIPE_AT91_TC, at91_ipipe_divisor,
	       divided_freq / 1000000, divided_freq % 1000000,
	       (unsigned) wrap_ns / 1000000, (unsigned) wrap_ns % 1000000);

	/* Add a 1ms margin. It means that when an interrupt occurs, update_tsc
	   must be called within 1ms. update_tsc is called by acktimer when no
	   higher domain handles the timer, and called through set_dec when a
	   higher domain handles the timer. */
	wrap_ns -= 1000000;
	/* Set up the interrupt. */

	if (host_timer && host_timer->features & CLOCK_EVT_FEAT_ONESHOT
	    && host_timer->max_delta_ns > wrap_ns)
		host_timer->max_delta_ns = wrap_ns;	

	at91_itimer.freq = divided_freq;
	at91_itimer.min_delay_ticks = ipipe_timer_ns2ticks(&at91_itimer, 2000);
	max_delta_ticks = ipipe_timer_ns2ticks(&at91_itimer, wrap_ns);

	tsc_info.freq = divided_freq;

	at91_pic_muter_register();
}

static int __init at91_ipipe_init(void)
{
	unsigned long at91_tc_pbase = 0;
	unsigned index, block;
	struct atmel_tc *tc;
	unsigned short v;
	int ret;

	index = CONFIG_IPIPE_AT91_TC % 3;
	block = CONFIG_IPIPE_AT91_TC / 3;

	tc = atmel_tc_alloc(block, "at91_ipipe");
	if (tc == NULL) {
		printk(KERN_ERR "I-pipe: could not reserve TC block %d\n", 
			block);
		return -ENODEV;
	}
	at91_tc_base = tc->regs;
	at91_tc_pbase = tc->iomem->start;
	at91_itimer.irq = tc->irq[index];

	ret = clk_prepare_enable(tc->clk[index]);
	if (ret < 0)
		goto err_free_tc;
	
	/* Disable the channel */
	at91_tc_write(CCR, ATMEL_TC_CLKDIS);

	/* Disable all interrupts. */
	at91_tc_write(IDR, ~0ul);

	/* No Sync. */
	at91_tc_write(BCR, 0);

	/* program NO signal on XCN */
	v = __raw_readl(at91_tc_base + ATMEL_TC_BMR);
	v &= ~TCNXCNS(index, 3);
	v |= TCNXCNS(index, 1); /* AT91_TC_TCNXCNS_NONE */
	__raw_writel(v, at91_tc_base + ATMEL_TC_BMR);

	/* Use the clock selected as input clock. */
	at91_tc_write(CMR, tc_timer_clock);

	/* Load the TC register C. */
	write_RC(0xffff);

	/* Enable the channel. */
	at91_tc_write(CCR, ATMEL_TC_CLKEN | ATMEL_TC_SWTRG);

	ipipe_timer_register(&at91_itimer);

	tsc_info.counter_vaddr = 
		(unsigned long)(at91_tc_base + ATMEL_TC_REG(index, CV));
	tsc_info.u.counter_paddr = (at91_tc_pbase + ATMEL_TC_REG(index, CV));
	__ipipe_tsc_register(&tsc_info);

	return 0;

err_free_tc:
	atmel_tc_free(tc);
	return ret;
}
subsys_initcall(at91_ipipe_init);
