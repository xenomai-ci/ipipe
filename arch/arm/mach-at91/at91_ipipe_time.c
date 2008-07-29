/*
 * linux/arch/arm/mach-at91/at91_ipipe_time.c
 *
 * Copyright (C) 2007 Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>
 *
 * Adaptation to AT91SAM926x:
 * Copyright (C) 2007 Gregory CLEMENT, Adeneo
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/clockchips.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/mach/time.h>

#include <asm/arch/at91_st.h>

#include <linux/clk.h>
#include <linux/stringify.h>
#include <linux/err.h>
#include <linux/console.h>
#include <linux/module.h>
#include <asm/arch/at91_tc.h>
#include <asm/arch/at91_pit.h>
#include "clock.h"

#if defined(CONFIG_ARCH_AT91RM9200)
#define AT91_ID_TC0 AT91RM9200_ID_TC0
#define AT91_ID_TC1 AT91RM9200_ID_TC1
#define AT91_ID_TC2 AT91RM9200_ID_TC2
#elif defined(CONFIG_ARCH_AT91SAM9260)
#define AT91_ID_TC0 AT91SAM9260_ID_TC0
#define AT91_ID_TC1 AT91SAM9260_ID_TC1
#define AT91_ID_TC2 AT91SAM9260_ID_TC2
#elif defined(CONFIG_ARCH_AT91SAM9261)
#define AT91_ID_TC0 AT91SAM9261_ID_TC0
#define AT91_ID_TC1 AT91SAM9261_ID_TC1
#define AT91_ID_TC2 AT91SAM9261_ID_TC2
#elif defined(CONFIG_ARCH_AT91SAM9263)
#define AT91_ID_TC0 AT91SAM9263_ID_TCB
#define AT91_ID_TC1 AT91SAM9263_ID_TCB
#define AT91_ID_TC2 AT91SAM9263_ID_TCB
#elif defined(CONFIG_ARCH_AT91SAM9RL)
#define AT91_ID_TC0 AT91SAM9RL_ID_TC0
#define AT91_ID_TC1 AT91SAM9RL_ID_TC1
#define AT91_ID_TC2 AT91SAM9RL_ID_TC2
#elif defined(CONFIG_ARCH_AT91X40)
#define AT91_ID_TC0 AT91X40_ID_TC0
#define AT91_ID_TC1 AT91X40_ID_TC1
#define AT91_ID_TC2 AT91X40_ID_TC2
#else
#error "AT91 processor unsupported by Adeos"
#endif

#if (CONFIG_IPIPE_AT91_TC==0)
#   define KERNEL_TIMER_IRQ_NUM AT91_ID_TC0
#elif (CONFIG_IPIPE_AT91_TC==1)
#   define KERNEL_TIMER_IRQ_NUM AT91_ID_TC1
#elif (CONFIG_IPIPE_AT91_TC==2)
#   define KERNEL_TIMER_IRQ_NUM AT91_ID_TC2
#else
#error IPIPE_AT91_TC must be 0, 1 or 2.
#endif

#ifdef CONFIG_NO_IDLE_HZ
#error "dynamic tick timer not yet supported with IPIPE"
#endif /* CONFIG_NO_IDLE_HZ */

#define TCNXCNS(timer,v) ((v) << ((timer)<<1))
#define AT91_TC_REG_MASK (0xffff)

static unsigned long last_CV;

union tsc_reg {
#ifdef __BIG_ENDIAN
	struct {
		unsigned long high;
		unsigned short mid;
		unsigned short low;
	};
#else /* __LITTLE_ENDIAN */
	struct {
		unsigned short low;
		unsigned short mid;
		unsigned long high;
	};
#endif /* __LITTLE_ENDIAN */
	unsigned long long full;
};
static struct clock_event_device clkevt;

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
	info->u.fr.counter =
		(unsigned *)
		(AT91_BASE_TCB0 + 0x40 * CONFIG_IPIPE_AT91_TC + AT91_TC_CV);
	info->u.fr.mask = AT91_TC_REG_MASK;
	info->u.fr.tsc = &tsc->full;
}
#endif /* !CONFIG_SMP */

static inline unsigned int at91_tc_read(unsigned int reg_offset)
{
	unsigned long addr =
		(AT91_VA_BASE_TCB0 + 0x40 * CONFIG_IPIPE_AT91_TC);

	return readl((void __iomem *)(addr + reg_offset));
}

static inline void at91_tc_write(unsigned int reg_offset, unsigned long value)
{
	unsigned long addr =
		(AT91_VA_BASE_TCB0 + 0x40 * CONFIG_IPIPE_AT91_TC);

	writel(value, (void __iomem *)(addr + reg_offset));
}

#define read_CV() at91_tc_read(AT91_TC_CV)
#define read_RC() at91_tc_read(AT91_TC_RC)
#define write_RC(value) at91_tc_write(AT91_TC_RC, value)

int __ipipe_mach_timerint = KERNEL_TIMER_IRQ_NUM;
EXPORT_SYMBOL(__ipipe_mach_timerint);

int __ipipe_mach_timerstolen = 0;
EXPORT_SYMBOL(__ipipe_mach_timerstolen);

unsigned int __ipipe_mach_ticks_per_jiffy = LATCH;
EXPORT_SYMBOL(__ipipe_mach_ticks_per_jiffy);

static int at91_timer_initialized;

/*
 * IRQ handler for the timer.
 */
static irqreturn_t at91_timer_interrupt(int irq, void *dev_id)
{
	/*
	 * - if Linux is running under ipipe, but it still has the control over
	 *   the timer (no Xenomai for example), then reprogram the timer (ipipe
	 *   has already acked it)
	 * - if some other domain has taken over the timer, then do nothing
	 *   (ipipe has acked it, and the other domain has reprogramed it)
	 */

	while (((read_CV() - last_CV) & AT91_TC_REG_MASK) >= LATCH) {
		clkevt.event_handler(&clkevt);
		last_CV = (last_CV + LATCH) & AT91_TC_REG_MASK;
	}
	if (!__ipipe_mach_timerstolen)
		write_RC((last_CV + LATCH) & AT91_TC_REG_MASK);

	return IRQ_HANDLED;
}

static irqreturn_t at91_bad_freq(int irq, void *dev_id)
{
	static int ticks = 0;

	if (++ticks != HZ * 120) {
		if (!console_drivers || try_acquire_console_sem())
			return at91_timer_interrupt(irq, dev_id);
	
		release_console_sem();
	}

	panic("AT91 clock rate incorrectly set.\n"
	      "Please recompile with IPIPE_AT91_MCK set to %lu Hz.",
	      clk_get_rate(clk_get(NULL, "mck")));
}

static struct irqaction at91_timer_irq = {
	.name		= "at91_tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= &at91_timer_interrupt
};

void __ipipe_mach_acktimer(void)
{
	union tsc_reg *local_tsc;
	unsigned short stamp;
	unsigned long flags;

	at91_tc_read(AT91_TC_SR);

	local_irq_save_hw(flags);
	local_tsc = &tsc[ipipe_processor_id()];
	stamp = read_CV();
	if (unlikely(stamp < local_tsc->low))
		local_tsc->full += AT91_TC_REG_MASK + 1;
	local_tsc->low = stamp;
	local_irq_restore_hw(flags);
}

notrace unsigned long long __ipipe_mach_get_tsc(void)
{
	if (likely(at91_timer_initialized)) {
		union tsc_reg *local_tsc, result;
		unsigned long stamp;

		local_tsc = &tsc[ipipe_processor_id()];

		__asm__ ("ldmia %1, %M0\n"
			 : "=r"(result.full), "+&r"(local_tsc)
			 : "m"(*local_tsc));
		barrier();
		stamp = read_CV();
		if (unlikely(stamp < result.low))
			result.full += AT91_TC_REG_MASK + 1;
		result.low = stamp;
		return result.full;
	}

	return 0;
}
EXPORT_SYMBOL(__ipipe_mach_get_tsc);

static struct clocksource clksrc = {
	.name   = "at91_tc" __stringify(CONFIG_IPIPE_AT91_TC),
	.rating = 250,
	.read   = __ipipe_mach_get_tsc,
	.mask   = CLOCKSOURCE_MASK(64),
	.shift  = 20,
	.flags  = CLOCK_SOURCE_IS_CONTINUOUS,
};

static void
at91_tc_clkevt_mode(enum clock_event_mode mode, struct clock_event_device *dev)
{
	/* Disable the channel */
	at91_tc_write(AT91_TC_CCR, AT91_TC_CLKDIS);

	/* Disable all interrupts. */
	at91_tc_write(AT91_TC_IDR, ~0ul);	

	if (mode == CLOCK_EVT_MODE_PERIODIC) {
		unsigned long v;

		/* No Sync. */
		at91_tc_write(AT91_TC_BCR, 0);

		/* program NO signal on XCN */
		v = readl((void __iomem *) (AT91_VA_BASE_TCB0 + AT91_TC_BMR));
		v &= ~TCNXCNS(CONFIG_IPIPE_AT91_TC, 3);
		v |= TCNXCNS(CONFIG_IPIPE_AT91_TC, 1); /* AT91_TC_TCNXCNS_NONE */
		writel(v, (void __iomem *) (AT91_VA_BASE_TCB0 + AT91_TC_BMR));

		/* Select TIMER_CLOCK3 (MCLK/32) as input frequency for TC. */
		at91_tc_write(AT91_TC_CMR, AT91_TC_TIMER_CLOCK3);

		/* Load the TC register C. */
		last_CV = 0;
		write_RC(LATCH);

		/* Enable CPCS interrupt. */
		at91_tc_write(AT91_TC_IER, AT91_TC_CPCS);

		/* Enable the channel. */
		at91_tc_write(AT91_TC_CCR, AT91_TC_CLKEN | AT91_TC_SWTRG);
	}
}

/*
 * Reprogram the timer
 */
void __ipipe_mach_set_dec(unsigned long delay)
{
	unsigned long flags;

	if (delay > 2) {
		local_irq_save_hw(flags);
		write_RC((read_CV() + delay) & AT91_TC_REG_MASK);
		local_irq_restore_hw(flags);
	} else
		ipipe_trigger_irq(KERNEL_TIMER_IRQ_NUM);
}
EXPORT_SYMBOL(__ipipe_mach_set_dec);

int __ipipe_check_tickdev(const char *devname)
{
	return !strcmp(devname, clkevt.name);
}

static struct clock_event_device clkevt = {
	.name		= "at91_tc" __stringify(CONFIG_IPIPE_AT91_TC),
	.features	= CLOCK_EVT_FEAT_PERIODIC,
	.shift		= 32,
	.rating		= 250,
	.cpumask	= CPU_MASK_CPU0,
	.set_mode	= at91_tc_clkevt_mode,
};

void __ipipe_mach_release_timer(void)
{
	__ipipe_mach_set_dec(__ipipe_mach_ticks_per_jiffy);
}
EXPORT_SYMBOL(__ipipe_mach_release_timer);

unsigned long __ipipe_mach_get_dec(void)
{
	return (read_RC() - read_CV()) & AT91_TC_REG_MASK;
}

static struct clk *tc, local_tc = {
#ifndef CONFIG_ARCH_AT91SAM9263
	.name		= "tc" __stringify(CONFIG_IPIPE_AT91_TC) "_clk",
#else
	/* at91sam9263 only has a single TCB clock. */
	.name		= "tcb_clk",
#endif
	.users          = 0,
	.type		= CLK_TYPE_PERIPHERAL,
	.pmc_mask       = 1 << (KERNEL_TIMER_IRQ_NUM),
};

void __init at91_timer_init(void)
{
	/* Disable (boot loader) timer interrupts. */
#if defined(CONFIG_ARCH_AT91RM9200)
	at91_sys_write(AT91_ST_IDR, AT91_ST_PITS | AT91_ST_WDOVF | AT91_ST_RTTINC | AT91_ST_ALMS);
	(void) at91_sys_read(AT91_ST_SR);	/* Clear any pending interrupts */	
#elif defined(CONFIG_ARCH_AT91SAM9260) || defined(CONFIG_ARCH_AT91SAM9261) \
	|| defined(CONFIG_ARCH_AT91SAM9263) || defined(CONFIG_ARCH_AT91SAM9RL)
	at91_sys_write(AT91_PIT_MR, 0);

	/* Clear any pending interrupts */
	(void) at91_sys_read(AT91_PIT_PIVR);
#endif /* CONFIG_ARCH_AT91SAM926x */

	if (clk_get_rate(clk_get(NULL, "mck")) != CONFIG_IPIPE_AT91_MCK)
		at91_timer_irq.handler = &at91_bad_freq;

	tc = clk_get(NULL, local_tc.name);
	if (IS_ERR(tc)) {
		tc = &local_tc;
		clk_register(tc);
	}
	clk_enable(tc);

	/* Set up the interrupt. */
	setup_irq(KERNEL_TIMER_IRQ_NUM, &at91_timer_irq);

#ifndef CONFIG_SMP
	tsc = (union tsc_reg *) __ipipe_tsc_area;
	barrier();
#endif /* CONFIG_SMP */

	at91_timer_initialized = 1;

	clkevt.mult = div_sc(CLOCK_TICK_RATE, NSEC_PER_SEC, clkevt.shift);
	clkevt.max_delta_ns = clockevent_delta2ns(AT91_TC_REG_MASK, &clkevt);
	clkevt.min_delta_ns = 0;
	clkevt.cpumask = cpumask_of_cpu(0);
	clockevents_register_device(&clkevt);

	clksrc.mult = clocksource_hz2mult(CLOCK_TICK_RATE, clksrc.shift);
	clocksource_register(&clksrc);
}

#ifdef CONFIG_ARCH_AT91RM9200
struct sys_timer at91rm9200_timer = {
#elif defined(CONFIG_ARCH_AT91SAM9260) || defined(CONFIG_ARCH_AT91SAM9261) \
	|| defined(CONFIG_ARCH_AT91SAM9263) || defined(CONFIG_ARCH_AT91SAM9RL)
struct sys_timer at91sam926x_timer = {
#elif defined(CONFIG_ARCH_AT91X40)
struct sys_timer at91x40_timer = {
#else
#error "Unknown machine"
#endif
	.init		= at91_timer_init,
	.suspend	= NULL,
	.resume		= NULL,
};
