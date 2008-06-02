/*
 * linux/arch/arm/mach-sa1100/time.c
 *
 * Copyright (C) 1998 Deborah Wallach.
 * Twiddles  (C) 1999 	Hugo Fiennes <hugo@empeg.com>
 * 
 * 2000/03/29 (C) Nicolas Pitre <nico@cam.org>
 *	Rewritten: big cleanup, much simpler, better HZ accuracy.
 *
 */
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/timex.h>
#include <linux/signal.h>
#include <linux/clocksource.h>

#include <asm/mach/time.h>
#include <asm/hardware.h>

#define RTC_DEF_DIVIDER		(32768 - 1)
#define RTC_DEF_TRIM            0

#ifdef CONFIG_IPIPE
#ifdef CONFIG_NO_IDLE_HZ
#error "dynamic tick timer not yet supported with IPIPE"
#endif /* CONFIG_NO_IDLE_HZ */
int __ipipe_mach_timerint = IRQ_OST0;
EXPORT_SYMBOL(__ipipe_mach_timerint);

int __ipipe_mach_timerstolen = 0;
EXPORT_SYMBOL(__ipipe_mach_timerstolen);

unsigned int __ipipe_mach_ticks_per_jiffy = LATCH;
EXPORT_SYMBOL(__ipipe_mach_ticks_per_jiffy);

static int sa1100_timer_initialized;
static unsigned long last_jiffy_time;

union tsc_reg {
#ifdef __BIG_ENDIAN
	struct {
		unsigned long high;
		unsigned long low;
	};
#else /* __LITTLE_ENDIAN */
	struct {
		unsigned long low;
		unsigned long high;
	};
#endif /* __LITTLE_ENDIAN */
	unsigned long long full;
};

#ifdef CONFIG_SMP
static union tsc_reg tsc[NR_CPUS];

void __ipipe_mach_get_tscinfo(struct __ipipe_tscinfo *info)
{
	info->type = IPIPE_TSC_TYPE_NONE;
}

#else /* !CONFIG_SMP */
static union tsc_reg *tsc;

void __ipipe_mach_get_tscinfo(struct __ipipe_tscinfo *info)
{
	info->type = IPIPE_TSC_TYPE_FREERUNNING;
	info->u.fr.counter = (unsigned *)0x90000010;
	info->u.fr.mask = 0xffffffff;
	info->u.fr.tsc = &tsc->full;
}
#endif /* !CONFIG_SMP */
#endif /* CONFIG_IPIPE */

static int sa1100_set_rtc(void)
{
	unsigned long current_time = xtime.tv_sec;

	if (RTSR & RTSR_ALE) {
		/* make sure not to forward the clock over an alarm */
		unsigned long alarm = RTAR;
		if (current_time >= alarm && alarm >= RCNR)
			return -ERESTARTSYS;
	}
	RCNR = current_time;
	return 0;
}

#ifdef CONFIG_NO_IDLE_HZ
static unsigned long initial_match;
static int match_posponed;
#endif

static irqreturn_t
sa1100_timer_interrupt(int irq, void *dev_id)
{
	unsigned int next_match;

#ifdef CONFIG_NO_IDLE_HZ
	if (match_posponed) {
		match_posponed = 0;
		OSMR0 = initial_match;
	}
#endif

	/*
	 * Loop until we get ahead of the free running timer.
	 * This ensures an exact clock tick count and time accuracy.
	 * Since IRQs are disabled at this point, coherence between
	 * lost_ticks(updated in do_timer()) and the match reg value is
	 * ensured, hence we can use do_gettimeofday() from interrupt
	 * handlers.
	 */
	/*
	 * - if Linux is running natively (no ipipe), ack and reprogram the timer
	 * - if Linux is running under ipipe, but it still has the control over
	 *   the timer (no Xenomai for example), then reprogram the timer (ipipe
	 *   has already acked it)
	 * - if some other domain has taken over the timer, then do nothing
	 *   (ipipe has acked it, and the other domain has reprogramed it)
	 */
	do {
		timer_tick();
#ifndef CONFIG_IPIPE
		OSSR = OSSR_M0;  /* Clear match on timer 0 */
#else /* CONFIG_IPIPE */
		last_jiffy_time += LATCH;
		if (__ipipe_mach_timerstolen)
			next_match = last_jiffy_time + LATCH;
		else
#endif /* CONFIG_IPIPE */
		next_match = (OSMR0 += LATCH);
	} while ((signed long)(next_match - OSCR) <= 0);

	return IRQ_HANDLED;
}

static struct irqaction sa1100_timer_irq = {
	.name		= "SA11xx Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= sa1100_timer_interrupt,
};

static cycle_t sa1100_read_oscr(void)
{
	return OSCR;
}

static struct clocksource cksrc_sa1100_oscr = {
	.name		= "oscr",
	.rating		= 200,
	.read		= sa1100_read_oscr,
	.mask		= CLOCKSOURCE_MASK(32),
	.shift		= 20,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init sa1100_timer_init(void)
{
	unsigned long flags;

	set_rtc = sa1100_set_rtc;

	OIER = 0;		/* disable any timer interrupts */
	OSSR = 0xf;		/* clear status on all timers */
	setup_irq(IRQ_OST0, &sa1100_timer_irq);
	local_irq_save(flags);
	OIER = OIER_E0;		/* enable match on timer 0 to cause interrupts */
#ifndef CONFIG_IPIPE
	OSMR0 = OSCR + LATCH;	/* set initial match */
#else /* CONFIG_IPIPE */
	last_jiffy_time = OSCR;
	OSMR0 = last_jiffy_time + LATCH;
#endif /* CONFIG_IPIPE */
	local_irq_restore(flags);

#ifdef CONFIG_IPIPE
#ifndef CONFIG_SMP
	tsc = (union tsc_reg *) __ipipe_tsc_area;
	barrier();
#endif /* CONFIG_SMP */

	sa1100_timer_initialized = 1;
#endif /* CONFIG_IPIPE */

	cksrc_sa1100_oscr.mult =
		clocksource_hz2mult(CLOCK_TICK_RATE, cksrc_sa1100_oscr.shift);

	clocksource_register(&cksrc_sa1100_oscr);
}

#ifdef CONFIG_NO_IDLE_HZ
static int sa1100_dyn_tick_enable_disable(void)
{
	/* nothing to do */
	return 0;
}

static void sa1100_dyn_tick_reprogram(unsigned long ticks)
{
	if (ticks > 1) {
		initial_match = OSMR0;
		OSMR0 = initial_match + ticks * LATCH;
		match_posponed = 1;
	}
}

static irqreturn_t
sa1100_dyn_tick_handler(int irq, void *dev_id)
{
	if (match_posponed) {
		match_posponed = 0;
		OSMR0 = initial_match;
		if ((signed long)(initial_match - OSCR) <= 0)
			return sa1100_timer_interrupt(irq, dev_id);
	}
	return IRQ_NONE;
}

static struct dyn_tick_timer sa1100_dyn_tick = {
	.enable		= sa1100_dyn_tick_enable_disable,
	.disable	= sa1100_dyn_tick_enable_disable,
	.reprogram	= sa1100_dyn_tick_reprogram,
	.handler	= sa1100_dyn_tick_handler,
};
#endif

#ifdef CONFIG_PM
unsigned long osmr[4], oier;

static void sa1100_timer_suspend(void)
{
	osmr[0] = OSMR0;
	osmr[1] = OSMR1;
	osmr[2] = OSMR2;
	osmr[3] = OSMR3;
	oier = OIER;
}

static void sa1100_timer_resume(void)
{
	OSSR = 0x0f;
	OSMR0 = osmr[0];
	OSMR1 = osmr[1];
	OSMR2 = osmr[2];
	OSMR3 = osmr[3];
	OIER = oier;

	/*
	 * OSMR0 is the system timer: make sure OSCR is sufficiently behind
	 */
	OSCR = OSMR0 - LATCH;
}
#else
#define sa1100_timer_suspend NULL
#define sa1100_timer_resume NULL
#endif

struct sys_timer sa1100_timer = {
	.init		= sa1100_timer_init,
	.suspend	= sa1100_timer_suspend,
	.resume		= sa1100_timer_resume,
#ifdef CONFIG_NO_IDLE_HZ
	.dyn_tick	= &sa1100_dyn_tick,
#endif
};

#ifdef CONFIG_IPIPE
void __ipipe_mach_acktimer(void)
{
	union tsc_reg *local_tsc;
	unsigned long stamp, flags;

	OSSR = OSSR_M0;  /* Clear match on timer 0 */

	local_irq_save_hw(flags);
	local_tsc = &tsc[ipipe_processor_id()];
	stamp = OSCR;
	if (unlikely(stamp < local_tsc->low))
		/* 32 bit counter wrapped, increment high word. */
		local_tsc->high++;
	local_tsc->low = stamp;
	local_irq_restore_hw(flags);
}

notrace unsigned long long __ipipe_mach_get_tsc(void)
{
	if (likely(sa1100_timer_initialized)) {
		union tsc_reg *local_tsc, result;
		unsigned long stamp;

		local_tsc = &tsc[ipipe_processor_id()];

		__asm__ ("ldmia %1, %M0\n"
			 : "=r"(result.full), "+&r"(local_tsc)
			 : "m"(*local_tsc));
		barrier();
		stamp = OSCR;
		if (unlikely(stamp < result.low))
			/* 32 bit counter wrapped, increment high word. */
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

	local_irq_save_hw(flags);
        OSMR0 = delay + OSCR;
	local_irq_restore_hw(flags);
}
EXPORT_SYMBOL(__ipipe_mach_set_dec);

void __ipipe_mach_release_timer(void)
{
       __ipipe_mach_set_dec(__ipipe_mach_ticks_per_jiffy);
}
EXPORT_SYMBOL(__ipipe_mach_release_timer);

unsigned long __ipipe_mach_get_dec(void)
{
	return OSMR0 - OSCR;
}
#endif /* CONFIG_IPIPE */
