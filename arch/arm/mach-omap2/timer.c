/*
 * linux/arch/arm/mach-omap2/timer.c
 *
 * OMAP2 GP timer support.
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * Update to use new clocksource/clockevent layers
 * Author: Kevin Hilman, MontaVista Software, Inc. <source@mvista.com>
 * Copyright (C) 2007 MontaVista Software, Inc.
 *
 * Original driver:
 * Copyright (C) 2005 Nokia Corporation
 * Author: Paul Mundt <paul.mundt@nokia.com>
 *         Juha Yrjölä <juha.yrjola@nokia.com>
 * OMAP Dual-mode timer framework support by Timo Teras
 *
 * Some parts based off of TI's 24xx code:
 *
 * Copyright (C) 2004-2009 Texas Instruments, Inc.
 *
 * Roughly modelled after the OMAP1 MPU timer code.
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/ipipe.h>

#include <asm/mach/time.h>
#include <plat/dmtimer.h>
#include <asm/localtimer.h>
#include <asm/sched_clock.h>
#include <plat/common.h>
#include <plat/omap_hwmod.h>

/* Parent clocks, eventually these will come from the clock framework */

#define OMAP2_MPU_SOURCE	"sys_ck"
#define OMAP3_MPU_SOURCE	OMAP2_MPU_SOURCE
#define OMAP4_MPU_SOURCE	"sys_clkin_ck"
#define OMAP2_32K_SOURCE	"func_32k_ck"
#define OMAP3_32K_SOURCE	"omap_32k_fck"
#define OMAP4_32K_SOURCE	"sys_32k_ck"

#ifdef CONFIG_OMAP_32K_TIMER
#define OMAP2_CLKEV_SOURCE	OMAP2_32K_SOURCE
#define OMAP3_CLKEV_SOURCE	OMAP3_32K_SOURCE
#define OMAP4_CLKEV_SOURCE	OMAP4_32K_SOURCE
#define OMAP3_SECURE_TIMER	12
#else
#define OMAP2_CLKEV_SOURCE	OMAP2_MPU_SOURCE
#define OMAP3_CLKEV_SOURCE	OMAP3_MPU_SOURCE
#define OMAP4_CLKEV_SOURCE	OMAP4_MPU_SOURCE
#define OMAP3_SECURE_TIMER	1
#endif

/* MAX_GPTIMER_ID: number of GPTIMERs on the chip */
#define MAX_GPTIMER_ID		12

u32 sys_timer_reserved;

/* Clockevent code */

static struct omap_dm_timer clkev;
static struct clock_event_device clockevent_gpt;

#if defined(CONFIG_IPIPE) && !defined(CONFIG_SMP)
int __ipipe_mach_timerint;
EXPORT_SYMBOL(__ipipe_mach_timerint);

unsigned int __ipipe_mach_ticks_per_jiffy;
EXPORT_SYMBOL(__ipipe_mach_ticks_per_jiffy);

int __ipipe_mach_timerstolen;
EXPORT_SYMBOL(__ipipe_mach_timerstolen);
#endif /* CONFIG_IPIPE && !CONFIG_SMP */

static irqreturn_t omap2_gp_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &clockevent_gpt;

#if !defined(CONFIG_IPIPE) || defined(CONFIG_SMP)
	__omap_dm_timer_write_status(clkev.io_base, OMAP_TIMER_INT_OVERFLOW);
#else /* CONFIG_IPIPE && !SMP */
	__ipipe_tsc_update();
#endif /* CONFIG_IPIPE && !SMP */

	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static struct irqaction omap2_gp_timer_irq = {
	.name		= "gp timer",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= omap2_gp_timer_interrupt,
};

static int omap2_gp_timer_set_next_event(unsigned long cycles,
					 struct clock_event_device *evt)
{
	__omap_dm_timer_load_start(clkev.io_base, OMAP_TIMER_CTRL_ST,
						0xffffffff - cycles, 0);

	return 0;
}

static void omap2_gp_timer_set_mode(enum clock_event_mode mode,
				    struct clock_event_device *evt)
{
	u32 period;

	__omap_dm_timer_stop(clkev.io_base, 0, clkev.rate);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		period = clkev.rate / HZ;
		period -= 1;
		/* Looks like we need to first set the load value separately */
		__omap_dm_timer_write(clkev.io_base, OMAP_TIMER_LOAD_REG,
					0xffffffff - period, 0);
		__omap_dm_timer_load_start(clkev.io_base,
					OMAP_TIMER_CTRL_AR | OMAP_TIMER_CTRL_ST,
						0xffffffff - period, 0);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static struct clock_event_device clockevent_gpt = {
	.name		= "gp timer",
	.features       = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.shift		= 32,
	.set_next_event	= omap2_gp_timer_set_next_event,
	.set_mode	= omap2_gp_timer_set_mode,
};

static int __init omap_dm_timer_init_one(struct omap_dm_timer *timer,
					 int gptimer_id,
					 const char *fck_source, int posted)
{
	char name[10]; /* 10 = sizeof("gptXX_Xck0") */
	struct omap_hwmod *oh;
	size_t size;
	int res = 0;

	sprintf(name, "timer%d", gptimer_id);
	omap_hwmod_setup_one(name);
	oh = omap_hwmod_lookup(name);
	if (!oh)
		return -ENODEV;

	timer->irq = oh->mpu_irqs[0].irq;
	timer->phys_base = oh->slaves[0]->addr->pa_start;
	size = oh->slaves[0]->addr->pa_end - timer->phys_base;

	/* Static mapping, never released */
	timer->io_base = ioremap(timer->phys_base, size);
	if (!timer->io_base)
		return -ENXIO;

	/* After the dmtimer is using hwmod these clocks won't be needed */
	sprintf(name, "gpt%d_fck", gptimer_id);
	timer->fclk = clk_get(NULL, name);
	if (IS_ERR(timer->fclk))
		return -ENODEV;

	sprintf(name, "gpt%d_ick", gptimer_id);
	timer->iclk = clk_get(NULL, name);
	if (IS_ERR(timer->iclk)) {
		clk_put(timer->fclk);
		return -ENODEV;
	}

	omap_hwmod_enable(oh);

	sys_timer_reserved |= (1 << (gptimer_id - 1));

	if (gptimer_id != 12) {
		struct clk *src;

		src = clk_get(NULL, fck_source);
		if (IS_ERR(src)) {
			res = -EINVAL;
		} else {
			res = __omap_dm_timer_set_source(timer->fclk, src);
			if (IS_ERR_VALUE(res))
				pr_warning("%s: timer%i cannot set source\n",
						__func__, gptimer_id);
			clk_put(src);
		}
	}
	__omap_dm_timer_reset(timer->io_base, 1, 1);
	timer->posted = posted;

	timer->rate = clk_get_rate(timer->fclk);

	timer->reserved = 1;

	return res;
}

#ifdef CONFIG_IPIPE
static struct __ipipe_tscinfo __maybe_unused tsc_info = {
	.type = IPIPE_TSC_TYPE_FREERUNNING,
	.u = {
		{
			.mask = 0xffffffff,
		},
	},
};

#ifndef CONFIG_SMP
int __ipipe_check_tickdev(const char *devname)
{
	return !strcmp(devname, clockevent_gpt.name);
}
#endif /* !CONFIG_SMP */
#endif /* CONFIG_IPIPE */

static void __init omap2_gp_clockevent_init(int gptimer_id,
						const char *fck_source)
{
	int res;

	res = omap_dm_timer_init_one(&clkev, gptimer_id, fck_source, 0);
	BUG_ON(res);

#if defined(CONFIG_IPIPE) && !defined(CONFIG_SMP)
	__ipipe_mach_timerint = clkev.irq;
	__ipipe_mach_ticks_per_jiffy = (clkev.rate + HZ / 2) / HZ;
#endif /* CONFIG_IPIPE && !CONFIG_SMP*/

	omap2_gp_timer_irq.dev_id = (void *)&clkev;
	setup_irq(clkev.irq, &omap2_gp_timer_irq);

	__omap_dm_timer_int_enable(clkev.io_base, OMAP_TIMER_INT_OVERFLOW);

	clockevent_gpt.mult = div_sc(clkev.rate, NSEC_PER_SEC,
				     clockevent_gpt.shift);
	clockevent_gpt.max_delta_ns =
		clockevent_delta2ns(0xffffffff, &clockevent_gpt);
	clockevent_gpt.min_delta_ns =
		clockevent_delta2ns(3, &clockevent_gpt);
		/* Timer internal resynch latency. */

	clockevent_gpt.cpumask = cpumask_of(0);
	clockevents_register_device(&clockevent_gpt);

	pr_info("OMAP clockevent source: GPTIMER%d at %lu Hz\n",
		gptimer_id, clkev.rate);
}

/* Clocksource code */

#ifdef CONFIG_OMAP_32K_TIMER
/*
 * When 32k-timer is enabled, don't use GPTimer for clocksource
 * instead, just leave default clocksource which uses the 32k
 * sync counter.  See clocksource setup in plat-omap/counter_32k.c
 */

static void __init omap2_gp_clocksource_init(int unused, const char *dummy)
{
	omap_init_clocksource_32k();
}

#else

static struct omap_dm_timer clksrc;

/*
 * clocksource
 */
static DEFINE_CLOCK_DATA(cd);
static cycle_t clocksource_read_cycles(struct clocksource *cs)
{
	return (cycle_t)__omap_dm_timer_read_counter(clksrc.io_base, 1);
}

static struct clocksource clocksource_gpt = {
	.name		= "gp timer",
	.rating		= 300,
	.read		= clocksource_read_cycles,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void notrace dmtimer_update_sched_clock(void)
{
	u32 cyc;

	cyc = __omap_dm_timer_read_counter(clksrc.io_base, 1);

	update_sched_clock(&cd, cyc, (u32)~0);
}

unsigned long long notrace sched_clock(void)
{
	u32 cyc = 0;

	if (clksrc.reserved)
		cyc = __omap_dm_timer_read_counter(clksrc.io_base, 1);

	return cyc_to_sched_clock(&cd, cyc, (u32)~0);
}

/* Setup free-running counter for clocksource */
static void __init omap2_gp_clocksource_init(int gptimer_id,
						const char *fck_source)
{
	int res;

	res = omap_dm_timer_init_one(&clksrc, gptimer_id, fck_source, 1);
	BUG_ON(res);

	pr_info("OMAP clocksource: GPTIMER%d at %lu Hz\n",
		gptimer_id, clksrc.rate);

#if defined(CONFIG_IPIPE) && !defined(CONFIG_SMP)
	tsc_info.freq = clksrc.rate;
	tsc_info.counter_vaddr =
		(unsigned long)clksrc.io_base + (OMAP_TIMER_COUNTER_REG & 0xff);
	tsc_info.u.counter_paddr =
		clksrc.phys_base + (OMAP_TIMER_COUNTER_REG & 0xff);
	__ipipe_tsc_register(&tsc_info);
#endif

	__omap_dm_timer_load_start(clksrc.io_base,
			OMAP_TIMER_CTRL_ST | OMAP_TIMER_CTRL_AR, 0, 1);
	init_sched_clock(&cd, dmtimer_update_sched_clock, 32, clksrc.rate);

	if (clocksource_register_hz(&clocksource_gpt, clksrc.rate))
		pr_err("Could not register clocksource %s\n",
			clocksource_gpt.name);
}
#endif

#define OMAP_SYS_TIMER_INIT(name, clkev_nr, clkev_src,			\
				clksrc_nr, clksrc_src)			\
static void __init omap##name##_timer_init(void)			\
{									\
	omap2_gp_clockevent_init((clkev_nr), clkev_src);		\
	omap2_gp_clocksource_init((clksrc_nr), clksrc_src);		\
}

#define OMAP_SYS_TIMER(name)						\
struct sys_timer omap##name##_timer = {					\
	.init	= omap##name##_timer_init,				\
};

#ifdef CONFIG_ARCH_OMAP2
OMAP_SYS_TIMER_INIT(2, 1, OMAP2_CLKEV_SOURCE, 2, OMAP2_MPU_SOURCE)
OMAP_SYS_TIMER(2)
#endif

#ifdef CONFIG_ARCH_OMAP3
#ifndef CONFIG_IPIPE
OMAP_SYS_TIMER_INIT(3, 1, OMAP3_CLKEV_SOURCE, 2, OMAP3_MPU_SOURCE)
#else /* CONFIG_IPIPE */
OMAP_SYS_TIMER_INIT(3, 2, OMAP3_CLKEV_SOURCE, 3, OMAP3_MPU_SOURCE)
#endif /* CONFIG_IPIPE */
OMAP_SYS_TIMER(3)
OMAP_SYS_TIMER_INIT(3_secure, OMAP3_SECURE_TIMER, OMAP3_CLKEV_SOURCE,
			2, OMAP3_MPU_SOURCE)
OMAP_SYS_TIMER(3_secure)
#endif

#ifdef CONFIG_ARCH_OMAP4
static void __init omap4_timer_init(void)
{
#ifdef CONFIG_LOCAL_TIMERS
	twd_base = ioremap(OMAP44XX_LOCAL_TWD_BASE, SZ_256);
	BUG_ON(!twd_base);
#endif
	omap2_gp_clockevent_init(1, OMAP4_CLKEV_SOURCE);
	omap2_gp_clocksource_init(2, OMAP4_MPU_SOURCE);
}
OMAP_SYS_TIMER(4)
#endif

#if defined(CONFIG_IPIPE) && !defined(CONFIG_SMP)
extern void printascii(const char *);

void __ipipe_mach_acktimer(void)
{
	__omap_dm_timer_write_status(clkev.io_base, OMAP_TIMER_INT_OVERFLOW);
	__omap_dm_timer_read_status(clkev.io_base);
}

/*
 * Reprogram the timer
 */

void __ipipe_mach_set_dec(unsigned long delay)
{
	if (delay > 3)
		__omap_dm_timer_load_start(clkev.io_base, OMAP_TIMER_CTRL_ST,
					   0xffffffff - delay, 0);
	else
		ipipe_raise_irq(__ipipe_mach_timerint);
}
EXPORT_SYMBOL(__ipipe_mach_set_dec);

void __ipipe_mach_release_timer(void)
{
	struct clock_event_device *ckdev = &clockevent_gpt;
	ckdev->set_mode(ckdev->mode, ckdev);
	if (ckdev->mode == CLOCK_EVT_MODE_ONESHOT)
		ckdev->set_next_event(__ipipe_mach_ticks_per_jiffy, ckdev);
}
EXPORT_SYMBOL(__ipipe_mach_release_timer);

unsigned long __ipipe_mach_get_dec(void)
{
	return 0xffffffff - __omap_dm_timer_read_counter(clkev.io_base, 1);
}
#endif /* CONFIG_IPIPE */
