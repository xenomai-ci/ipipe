/*
 * linux/arch/arm/mach-at91rm9200/at91rm9200_time.c
 *
 *  Copyright (C) 2003 SAN People
 *  Copyright (C) 2003 ATMEL
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/mach/time.h>

#include <asm/arch/at91_st.h>

static unsigned long last_crtr;

#ifdef CONFIG_IPIPE
#include <linux/clk.h>
#include <linux/console.h>
#include <linux/module.h>
#include <asm/arch/at91_tc.h>
#include "clock.h"

#ifdef CONFIG_NO_IDLE_HZ
#error "dynamic tick timer not yet supported with IPIPE"
#endif /* CONFIG_NO_IDLE_HZ */

#define TCNXCNS(timer,v) ((v) << ((timer)<<1))
#define AT91_TC_REG_MASK (0xffff)

#if (CONFIG_IPIPE_AT91_TC==0)
#   define KERNEL_TIMER_IRQ_NUM AT91RM9200_ID_TC0
#elif (CONFIG_IPIPE_AT91_TC==1)
#   define KERNEL_TIMER_IRQ_NUM AT91RM9200_ID_TC1
#elif (CONFIG_IPIPE_AT91_TC==2)
#   define KERNEL_TIMER_IRQ_NUM AT91RM9200_ID_TC2
#else
#error IPIPE_AT91_TC must be 0, 1 or 2.
#endif

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
#endif /* CONFIG_IPIPE */

/*
 * The ST_CRTR is updated asynchronously to the master clock.  It is therefore
 *  necessary to read it twice (with the same value) to ensure accuracy.
 */
static inline unsigned long read_CRTR(void) {
	unsigned long x1, x2;

	x2 = at91_sys_read(AT91_ST_CRTR);
	do {
		x1 = x2;
		x2 = at91_sys_read(AT91_ST_CRTR);
	} while (x1 != x2);

	return x1;
}

#ifndef CONFIG_IPIPE
/*
 * Returns number of microseconds since last timer interrupt.  Note that interrupts
 * will have been disabled by do_gettimeofday()
 *  'LATCH' is hwclock ticks (see CLOCK_TICK_RATE in timex.h) per jiffy.
 *  'tick' is usecs per jiffy (linux/timex.h).
 */
static unsigned long at91rm9200_gettimeoffset(void)
{
	unsigned long elapsed;

	elapsed = (read_CRTR() - last_crtr) & AT91_ST_ALMV;

	return (unsigned long)(elapsed * (tick_nsec / 1000)) / LATCH;
}

/*
 * IRQ handler for the timer.
 */
static irqreturn_t at91rm9200_timer_interrupt(int irq, void *dev_id)
{
	if (at91_sys_read(AT91_ST_SR) & AT91_ST_PITS) {	/* This is a shared interrupt */
		write_seqlock(&xtime_lock);

		while (((read_CRTR() - last_crtr) & AT91_ST_ALMV) >= LATCH) {
			timer_tick();
			last_crtr = (last_crtr + LATCH) & AT91_ST_ALMV;
		}

		write_sequnlock(&xtime_lock);

		return IRQ_HANDLED;
	}
	else
		return IRQ_NONE;		/* not handled */
}

static struct irqaction at91rm9200_timer_irq = {
	.name		= "at91_tick",
	.flags		= IRQF_SHARED | IRQF_DISABLED | IRQF_TIMER,
	.handler	= at91rm9200_timer_interrupt
};

void at91rm9200_timer_reset(void)
{
	last_crtr = 0;

	/* Real time counter incremented every 30.51758 microseconds */
	at91_sys_write(AT91_ST_RTMR, 1);

	/* Set Period Interval timer */
	at91_sys_write(AT91_ST_PIMR, LATCH);

	/* Clear any pending interrupts */
	(void) at91_sys_read(AT91_ST_SR);

	/* Enable Period Interval Timer interrupt */
	at91_sys_write(AT91_ST_IER, AT91_ST_PITS);
}

/*
 * Set up timer interrupt.
 */
void __init at91rm9200_timer_init(void)
{
	/* Disable all timer interrupts */
	at91_sys_write(AT91_ST_IDR, AT91_ST_PITS | AT91_ST_WDOVF | AT91_ST_RTTINC | AT91_ST_ALMS);
	(void) at91_sys_read(AT91_ST_SR);	/* Clear any pending interrupts */

	/* Make IRQs happen for the system timer */
	setup_irq(AT91_ID_SYS, &at91rm9200_timer_irq);

	/* Change the kernel's 'tick' value to 10009 usec. (the default is 10000) */
	tick_usec = (LATCH * 1000000) / CLOCK_TICK_RATE;

	/* Initialize and enable the timer interrupt */
	at91rm9200_timer_reset();
}

#ifdef CONFIG_PM
static void at91rm9200_timer_suspend(void)
{
	/* disable Period Interval Timer interrupt */
	at91_sys_write(AT91_ST_IDR, AT91_ST_PITS);
}
#else
#define at91rm9200_timer_suspend	NULL
#endif

struct sys_timer at91rm9200_timer = {
	.init		= at91rm9200_timer_init,
	.offset		= at91rm9200_gettimeoffset,
	.suspend	= at91rm9200_timer_suspend,
	.resume		= at91rm9200_timer_reset,
};

#else /* CONFIG_IPIPE */

/*
 * Returns number of microseconds since last timer interrupt.  Note that interrupts
 * will have been disabled by do_gettimeofday()
 *  'LATCH' is hwclock ticks (see CLOCK_TICK_RATE in timex.h) per jiffy.
 *  'tick' is usecs per jiffy (linux/timex.h).
 */
static unsigned long at91rm9200_gettimeoffset(void)
{
	unsigned long elapsed;

	elapsed = (read_CV() - last_crtr) & AT91_TC_REG_MASK;

	return (unsigned long) (elapsed * (tick_nsec / 1000)) / LATCH;
}

void __ipipe_mach_acktimer(void)
{
	at91_tc_read(AT91_TC_SR);
}

/*
 * IRQ handler for the timer.
 */
static irqreturn_t at91rm9200_timer_interrupt(int irq, void *dev_id)
{
	/*
	 * - if Linux is running under ipipe, but it still has the control over
	 *   the timer (no Xenomai for example), then reprogram the timer (ipipe
	 *   has already acked it)
	 * - if some other domain has taken over the timer, then do nothing
	 *   (ipipe has acked it, and the other domain has reprogramed it)
	 */

	write_seqlock(&xtime_lock);

	if (__ipipe_mach_timerstolen) {
		timer_tick();
		last_crtr = (last_crtr + LATCH) & AT91_TC_REG_MASK;
	} else {
		while (((read_CV() - last_crtr) & AT91_TC_REG_MASK) >= LATCH) {
			timer_tick();
			last_crtr = (last_crtr + LATCH) & AT91_TC_REG_MASK;
		}
		write_RC((last_crtr + LATCH) & AT91_TC_REG_MASK);
	}

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static irqreturn_t at91rm9200_bad_freq(int irq, void *dev_id)
{
	static int ticks = 0;

	if (++ticks != HZ * 120) {
		if (!console_drivers || try_acquire_console_sem())
			return at91rm9200_timer_interrupt(irq, dev_id);
	
		release_console_sem();
	}

	panic("AT91 clock rate incorrectly set.\n"
	      "Please recompile with IPIPE_AT91_MCK set to %lu Hz.",
	      clk_get_rate(clk_get(NULL, "mck")));
}

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

notrace unsigned long long __ipipe_mach_get_tsc(void)
{
	if (likely(at91_timer_initialized)) {
		unsigned long long result;
		union tsc_reg *local_tsc;
		unsigned short stamp;
		unsigned long flags;

		local_irq_save_hw_notrace(flags);
		local_tsc = &tsc[ipipe_processor_id()];
		stamp = read_CV();
		if (unlikely(stamp < local_tsc->low)) {
			if (unlikely(!++local_tsc->mid))
				/* 32 bit counter wrapped, increment high word. */
				local_tsc->high++;
		}
		local_tsc->low = stamp;
		result = local_tsc->full;
		local_irq_restore_hw_notrace(flags);

		return result;
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

	if (delay > 2) {
		local_irq_save_hw(flags);
		write_RC((read_CV() + delay) & AT91_TC_REG_MASK);
		local_irq_restore_hw(flags);
	} else
		ipipe_trigger_irq(KERNEL_TIMER_IRQ_NUM);
}
EXPORT_SYMBOL(__ipipe_mach_set_dec);

void __ipipe_mach_release_timer(void)
{
       __ipipe_mach_set_dec(__ipipe_mach_ticks_per_jiffy);
}
EXPORT_SYMBOL(__ipipe_mach_release_timer);

unsigned long __ipipe_mach_get_dec(void)
{
	return (read_RC() - read_CV()) & AT91_TC_REG_MASK;
}

static struct irqaction at91rm9200_timer_irq = {
	.name		= "at91_tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= &at91rm9200_timer_interrupt
};

static char clk_name [] = "tc%";

static struct clk tc = {
	.name		= (const char *) clk_name,
	.users          = 0,
	.type		= CLK_TYPE_PERIPHERAL,
	.pmc_mask       = 1 << (KERNEL_TIMER_IRQ_NUM),
};

void __init at91rm9200_timer_init(void)
{
	unsigned long v;
	
	/* Disable all timer interrupts */
	at91_sys_write(AT91_ST_IDR, AT91_ST_PITS | AT91_ST_WDOVF | AT91_ST_RTTINC | AT91_ST_ALMS);
	(void) at91_sys_read(AT91_ST_SR);	/* Clear any pending interrupts */

	if (clk_get_rate(clk_get(NULL, "mck")) != CONFIG_IPIPE_AT91_MCK)
		at91rm9200_timer_irq.handler = &at91rm9200_bad_freq;

	snprintf(clk_name, sizeof(clk_name), "tc%d", CONFIG_IPIPE_AT91_TC);
	clk_register(&tc);
	clk_enable(&tc);
	
	/* No Sync. */
	at91_tc_write(AT91_TC_BCR, 0);

	/* program NO signal on XCN */
	v = readl((void __iomem *) (AT91_VA_BASE_TCB0 + AT91_TC_BMR));
	v &= ~TCNXCNS(CONFIG_IPIPE_AT91_TC, 3);
	v |= TCNXCNS(CONFIG_IPIPE_AT91_TC, 1); /* AT91_TC_TCNXCNS_NONE */
	writel(v, (void __iomem *) (AT91_VA_BASE_TCB0 + AT91_TC_BMR));

	/* Disable the channel */
	at91_tc_write(AT91_TC_CCR, AT91_TC_CLKDIS);

	/* Select TIMER_CLOCK3 (MCLK/32) as input frequency for TC. */
	at91_tc_write(AT91_TC_CMR, AT91_TC_TIMER_CLOCK3);

	/* Disable all interrupts. */
	at91_tc_write(AT91_TC_IDR, ~0ul);

	/* Load the TC register C. */
	last_crtr = 0;
	write_RC(LATCH);

	/* Enable CPCS interrupt. */
	at91_tc_write(AT91_TC_IER, AT91_TC_CPCS);

	/* Set up the interrupt. */
	setup_irq(KERNEL_TIMER_IRQ_NUM, &at91rm9200_timer_irq);

	/* Enable the channel. */
	at91_tc_write(AT91_TC_CCR, AT91_TC_CLKEN | AT91_TC_SWTRG);

#ifndef CONFIG_SMP
	tsc = (union tsc_reg *) __ipipe_tsc_area;
	barrier();
#endif /* CONFIG_SMP */

	at91_timer_initialized = 1;
}

struct sys_timer at91rm9200_timer = {
	.init		= at91rm9200_timer_init,
	.offset		= at91rm9200_gettimeoffset,
	.suspend	= NULL,
	.resume		= NULL,
};
#endif /* CONFIG_IPIPE */
