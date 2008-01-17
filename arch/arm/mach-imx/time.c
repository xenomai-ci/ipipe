/*
 *  linux/arch/arm/mach-imx/time.c
 *
 *  Copyright (C) 2000-2001 Deep Blue Solutions
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/time.h>
#include <linux/clocksource.h>
#include <linux/module.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/leds.h>
#include <asm/irq.h>
#include <asm/mach/time.h>

#ifdef CONFIG_IPIPE
#ifdef CONFIG_NO_IDLE_HZ
#error "dynamic tick timer not yet supported with IPIPE"
#endif				/* CONFIG_NO_IDLE_HZ */
int __ipipe_mach_timerint = TIM1_INT;
EXPORT_SYMBOL(__ipipe_mach_timerint);

int __ipipe_mach_timerstolen = 0;
EXPORT_SYMBOL(__ipipe_mach_timerstolen);

unsigned int __ipipe_mach_ticks_per_jiffy = LATCH;
EXPORT_SYMBOL(__ipipe_mach_ticks_per_jiffy);

static int imx_timer_initialized;
static unsigned long last_jiffy_time;
union tsc_reg {
#ifdef __BIG_ENDIAN
	struct {
		unsigned long high;
		unsigned long low;
	};
#else				/* __LITTLE_ENDIAN */
	struct {
		unsigned long low;
		unsigned long high;
	};
#endif				/* __LITTLE_ENDIAN */
	unsigned long long full;
};
#ifdef CONFIG_SMP
static union tsc_reg tsc[NR_CPUS];

void __ipipe_mach_get_tscinfo(struct __ipipe_tscinfo *info)
{
	info->type = IPIPE_TSC_TYPE_NONE;
}
#else				/* !CONFIG_SMP */
static union tsc_reg *tsc;

void __ipipe_mach_get_tscinfo(struct __ipipe_tscinfo *info)
{
	info->type = IPIPE_TSC_TYPE_FREERUNNING;
	info->u.fr.counter = (unsigned *)(0x10 + IMX_TIM1_BASE);
	info->u.fr.mask = 0xffffffff;
	info->u.fr.tsc = &tsc->full;
}
#endif				/* !CONFIG_SMP */
#endif				/* CONFIG_IPIPE */

/* Use timer 1 as system timer */
#define TIMER_BASE IMX_TIM1_BASE

static unsigned long evt_diff;

/*
 * IRQ handler for the timer
 */
static irqreturn_t imx_timer_interrupt(int irq, void *dev_id)
{
	uint32_t tstat;

#ifdef CONFIG_IPIPE
	/*
	 * - if Linux is running natively (no ipipe), ack and reprogram the timer
	 * - if Linux is running under ipipe, but it still has the control over
	 *   the timer (no Xenomai for example), then reprogram the timer (ipipe
	 *   has already acked it)
	 * - if some other domain has taken over the timer, then do nothing
	 *   (ipipe has acked it, and the other domain has reprogramed it)
	 */
	if (__ipipe_mach_timerstolen)
		do {
			write_seqlock(&xtime_lock);
			timer_tick();
			write_sequnlock(&xtime_lock);
			last_jiffy_time += LATCH;
		} while (unlikely((int32_t) (last_jiffy_time + LATCH
					     - IMX_TCN(TIMER_BASE)) < 0));
	else
#else				/* !CONFIG_IPIPE */
	/* clear the interrupt */
	tstat = IMX_TSTAT(TIMER_BASE);
	IMX_TSTAT(TIMER_BASE) = 0;

	if (tstat & TSTAT_COMP) {
#endif
		do {
			write_seqlock(&xtime_lock);
			timer_tick();
			write_sequnlock(&xtime_lock);
#ifdef CONFIG_IPIPE
			last_jiffy_time += LATCH;
#endif				/*  CONFIG_IPIPE */
			IMX_TCMP(TIMER_BASE) += evt_diff;
		} while (unlikely((int32_t) (IMX_TCMP(TIMER_BASE)
					- IMX_TCN(TIMER_BASE)) < 0));
#ifndef CONFIG_IPIPE
	}
#endif				/*  CONFIG_IPIPE */

	return IRQ_HANDLED;
}

static struct irqaction imx_timer_irq = {
	.name		= "i.MX Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= imx_timer_interrupt,
};

/*
 * Set up timer hardware into expected mode and state.
 */
static void __init imx_timer_hardware_init(void)
{
	/*
	 * Initialise to a known state (all timers off, and timing reset)
	 */
	IMX_TCTL(TIMER_BASE) = 0;
	IMX_TPRER(TIMER_BASE) = 0;
	IMX_TCMP(TIMER_BASE) = LATCH - 1;

	IMX_TCTL(TIMER_BASE) = TCTL_FRR | TCTL_CLK_PCLK1 | TCTL_IRQEN | TCTL_TEN;
	evt_diff = LATCH;
}

cycle_t imx_get_cycles(void)
{
	return IMX_TCN(TIMER_BASE);
}

static struct clocksource clocksource_imx = {
	.name 		= "imx_timer1",
	.rating		= 200,
	.read		= imx_get_cycles,
	.mask		= 0xFFFFFFFF,
	.shift 		= 20,
	.is_continuous 	= 1,
};

static int __init imx_clocksource_init(void)
{
	clocksource_imx.mult =
		clocksource_hz2mult(imx_get_perclk1(), clocksource_imx.shift);
	clocksource_register(&clocksource_imx);

	return 0;
}

static void __init imx_timer_init(void)
{
	imx_timer_hardware_init();
	imx_clocksource_init();

	/*
	 * Make irqs happen for the system timer
	 */
	setup_irq(TIM1_INT, &imx_timer_irq);

#ifdef CONFIG_IPIPE
#ifndef CONFIG_SMP
	tsc = (union tsc_reg *)__ipipe_tsc_area;
	barrier();
#endif				/* CONFIG_SMP */
	imx_timer_initialized = 1;

#endif				/* CONFIG_IPIPE */
}

struct sys_timer imx_timer = {
	.init		= imx_timer_init,
};

#ifdef CONFIG_IPIPE
void __ipipe_mach_acktimer(void)
{
	uint32_t tstat;
	tstat = IMX_TSTAT(TIMER_BASE);
	IMX_TSTAT(TIMER_BASE) = 0;
	if (likely(imx_timer_initialized && (tstat & TSTAT_COMP) )) {
		union tsc_reg *local_tsc;
		unsigned long stamp, flags;

		local_irq_save_hw(flags);
		local_tsc = &tsc[ipipe_processor_id()];
		stamp = IMX_TCN(TIMER_BASE);
		if (unlikely(stamp < local_tsc->low))
			/* 32 bit counter wrapped, increment high word. */
			local_tsc->high++;
		local_tsc->low = stamp;
		local_irq_restore_hw(flags);
	}
}

notrace unsigned long long __ipipe_mach_get_tsc(void)
{
	if (likely(imx_timer_initialized)) {
		union tsc_reg *local_tsc, result;
		unsigned long stamp;

		local_tsc = &tsc[ipipe_processor_id()];

	      __asm__("ldmia %1, %M0\n"
		      : "=r"(result.full), "+&r"(local_tsc)
		      : "m"(*local_tsc));
		barrier();
		stamp = IMX_TCN(TIMER_BASE);
		if (unlikely(stamp < result.low))
			result.high++;
		result.low = stamp;
		return result.full;
	}
	return 0;
}

EXPORT_SYMBOL(__ipipe_mach_get_tsc);

/*
 * Reprogram the timer
 */
void __ipipe_mach_set_dec(unsigned long delay)
{
	unsigned long flags;
	if (delay > 8) {
		local_irq_save_hw(flags);
		IMX_TCMP(TIMER_BASE) = IMX_TCN(TIMER_BASE) + delay;
		local_irq_restore_hw(flags);
	} else
		ipipe_trigger_irq(TIM1_INT);
}

EXPORT_SYMBOL(__ipipe_mach_set_dec);

void __ipipe_mach_release_timer(void)
{
	__ipipe_mach_set_dec(__ipipe_mach_ticks_per_jiffy);
}

EXPORT_SYMBOL(__ipipe_mach_release_timer);

unsigned long __ipipe_mach_get_dec(void)
{
	return IMX_TCMP(TIMER_BASE) - IMX_TCN(TIMER_BASE);
}
#endif				/* CONFIG_IPIPE */
