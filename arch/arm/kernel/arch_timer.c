/*
 *  linux/arch/arm/kernel/arch_timer.c
 *
 *  Copyright (C) 2011 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
<<<<<<< HEAD
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/jiffies.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/ipipe.h>
#include <linux/ipipe_tickdev.h>
=======
#include <linux/types.h>
#include <linux/errno.h>
>>>>>>> v3.9

#include <asm/delay.h>
#include <asm/sched_clock.h>

#include <clocksource/arm_arch_timer.h>

<<<<<<< HEAD
enum ppi_nr {
	PHYS_SECURE_PPI,
	PHYS_NONSECURE_PPI,
	VIRT_PPI,
	HYP_PPI,
	MAX_TIMER_PPI
};

static int arch_timer_ppi[MAX_TIMER_PPI];

static struct clock_event_device __percpu **arch_timer_evt;
static struct delay_timer arch_delay_timer;

static bool arch_timer_use_virtual = true;

/*
 * Architected system timer support.
 */

#define ARCH_TIMER_CTRL_ENABLE		(1 << 0)
#define ARCH_TIMER_CTRL_IT_MASK		(1 << 1)
#define ARCH_TIMER_CTRL_IT_STAT		(1 << 2)

#define ARCH_TIMER_REG_CTRL		0
#define ARCH_TIMER_REG_FREQ		1
#define ARCH_TIMER_REG_TVAL		2

#define ARCH_TIMER_PHYS_ACCESS		0
#define ARCH_TIMER_VIRT_ACCESS		1

/*
 * These register accessors are marked inline so the compiler can
 * nicely work out which register we want, and chuck away the rest of
 * the code. At least it does so with a recent GCC (4.6.3).
 */
static inline void arch_timer_reg_write(const int access, const int reg, u32 val)
{
	if (access == ARCH_TIMER_PHYS_ACCESS) {
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			asm volatile("mcr p15, 0, %0, c14, c2, 1" : : "r" (val));
			break;
		case ARCH_TIMER_REG_TVAL:
			asm volatile("mcr p15, 0, %0, c14, c2, 0" : : "r" (val));
			break;
		}
	}

	if (access == ARCH_TIMER_VIRT_ACCESS) {
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			asm volatile("mcr p15, 0, %0, c14, c3, 1" : : "r" (val));
			break;
		case ARCH_TIMER_REG_TVAL:
			asm volatile("mcr p15, 0, %0, c14, c3, 0" : : "r" (val));
			break;
		}
	}

	isb();
}

static inline u32 arch_timer_reg_read(const int access, const int reg)
{
	u32 val = 0;

	if (access == ARCH_TIMER_PHYS_ACCESS) {
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			asm volatile("mrc p15, 0, %0, c14, c2, 1" : "=r" (val));
			break;
		case ARCH_TIMER_REG_TVAL:
			asm volatile("mrc p15, 0, %0, c14, c2, 0" : "=r" (val));
			break;
		case ARCH_TIMER_REG_FREQ:
			asm volatile("mrc p15, 0, %0, c14, c0, 0" : "=r" (val));
			break;
		}
	}

	if (access == ARCH_TIMER_VIRT_ACCESS) {
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			asm volatile("mrc p15, 0, %0, c14, c3, 1" : "=r" (val));
			break;
		case ARCH_TIMER_REG_TVAL:
			asm volatile("mrc p15, 0, %0, c14, c3, 0" : "=r" (val));
			break;
		}
	}

	return val;
}

static inline cycle_t arch_timer_counter_read(const int access)
{
	cycle_t cval = 0;

	if (access == ARCH_TIMER_PHYS_ACCESS)
		asm volatile("mrrc p15, 0, %Q0, %R0, c14" : "=r" (cval));

	if (access == ARCH_TIMER_VIRT_ACCESS)
		asm volatile("mrrc p15, 1, %Q0, %R0, c14" : "=r" (cval));

	return cval;
}

static inline cycle_t arch_counter_get_cntpct(void)
{
	return arch_timer_counter_read(ARCH_TIMER_PHYS_ACCESS);
}

static inline cycle_t arch_counter_get_cntvct(void)
{
	return arch_timer_counter_read(ARCH_TIMER_VIRT_ACCESS);
}

static int arch_timer_ack(const int access)
{
	unsigned long ctrl;
	ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL);
	if (ctrl & ARCH_TIMER_CTRL_IT_STAT) {
		ctrl |= ARCH_TIMER_CTRL_IT_MASK;
		arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl);
		return 1;
	}
	return 0;
}

#ifdef CONFIG_IPIPE
static DEFINE_PER_CPU(struct ipipe_timer, arch_itimer);
static struct __ipipe_tscinfo tsc_info = {
	.type = IPIPE_TSC_TYPE_FREERUNNING_ARCH,
	.u = {
		{
			.mask = 0xffffffffffffffff,
		},
	},
};

static void arch_itimer_ack_phys(void)
{
	arch_timer_ack(ARCH_TIMER_PHYS_ACCESS);
}

static void arch_itimer_ack_virt(void)
{
	arch_timer_ack(ARCH_TIMER_VIRT_ACCESS);
}
#endif /* CONFIG_IPIPE */

static irqreturn_t inline timer_handler(int irq, const int access,
					struct clock_event_device *evt)
{
	if (clockevent_ipipe_stolen(evt))
		goto stolen;

	if (arch_timer_ack(access)) {
#ifdef CONFIG_IPIPE
		struct ipipe_timer *itimer = __this_cpu_ptr(&arch_itimer);
		if (itimer->irq != irq)
			itimer->irq = irq;
#endif /* CONFIG_IPIPE */
	  stolen:
		__ipipe_tsc_update();
		evt->event_handler(evt);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static irqreturn_t arch_timer_handler_virt(int irq, void *dev_id)
{
	struct clock_event_device *evt = *(struct clock_event_device **)dev_id;

	return timer_handler(irq, ARCH_TIMER_VIRT_ACCESS, evt);
}

static irqreturn_t arch_timer_handler_phys(int irq, void *dev_id)
{
	struct clock_event_device *evt = *(struct clock_event_device **)dev_id;

	return timer_handler(irq, ARCH_TIMER_PHYS_ACCESS, evt);
}

static inline void timer_set_mode(const int access, int mode)
{
	unsigned long ctrl;
	switch (mode) {
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL);
		ctrl &= ~ARCH_TIMER_CTRL_ENABLE;
		arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl);
		break;
	default:
		break;
	}
}

static void arch_timer_set_mode_virt(enum clock_event_mode mode,
				     struct clock_event_device *clk)
{
	timer_set_mode(ARCH_TIMER_VIRT_ACCESS, mode);
}

static void arch_timer_set_mode_phys(enum clock_event_mode mode,
				     struct clock_event_device *clk)
{
	timer_set_mode(ARCH_TIMER_PHYS_ACCESS, mode);
}

static inline void set_next_event(const int access, unsigned long evt)
{
	unsigned long ctrl;
	ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL);
	ctrl |= ARCH_TIMER_CTRL_ENABLE;
	ctrl &= ~ARCH_TIMER_CTRL_IT_MASK;
	arch_timer_reg_write(access, ARCH_TIMER_REG_TVAL, evt);
	arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl);
}

static int arch_timer_set_next_event_virt(unsigned long evt,
					  struct clock_event_device *unused)
{
	set_next_event(ARCH_TIMER_VIRT_ACCESS, evt);
	return 0;
}

static int arch_timer_set_next_event_phys(unsigned long evt,
					  struct clock_event_device *unused)
{
	set_next_event(ARCH_TIMER_PHYS_ACCESS, evt);
	return 0;
}

static int __cpuinit arch_timer_setup(struct clock_event_device *clk)
{
	clk->features = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_C3STOP;
	clk->name = "arch_sys_timer";
	clk->rating = 450;
	if (arch_timer_use_virtual) {
		clk->irq = arch_timer_ppi[VIRT_PPI];
		clk->set_mode = arch_timer_set_mode_virt;
		clk->set_next_event = arch_timer_set_next_event_virt;
	} else {
		clk->irq = arch_timer_ppi[PHYS_SECURE_PPI];
		clk->set_mode = arch_timer_set_mode_phys;
		clk->set_next_event = arch_timer_set_next_event_phys;
	}

	clk->set_mode(CLOCK_EVT_MODE_SHUTDOWN, NULL);

#ifdef CONFIG_IPIPE
	clk->ipipe_timer = __this_cpu_ptr(&arch_itimer);
	if (arch_timer_use_virtual) {
		clk->ipipe_timer->irq = arch_timer_ppi[VIRT_PPI];
		clk->ipipe_timer->ack = arch_itimer_ack_virt;
	} else {
		clk->ipipe_timer->irq = arch_timer_ppi[PHYS_SECURE_PPI];
		clk->ipipe_timer->ack = arch_itimer_ack_phys;
	}
	clk->ipipe_timer->freq = arch_timer_rate;

	/* 
	 * Change CNTKCTL to give access to the physical counter to
	 * user-space, this has to be done once for each core.
	 */
	{
		unsigned ctl;
		asm volatile("mrc p15, 0, %0, c14, c1, 0" : "=r" (ctl));
		ctl |= 1;				    /* PL0PCTEN */
		asm volatile("mcr p15, 0, %0, c14, c1, 0" : : "r"(ctl));
		isb();
	}
#endif

	clockevents_config_and_register(clk, arch_timer_rate,
					0xf, 0x7fffffff);

	*__this_cpu_ptr(arch_timer_evt) = clk;

	if (arch_timer_use_virtual)
		enable_percpu_irq(arch_timer_ppi[VIRT_PPI], 0);
	else {
		enable_percpu_irq(arch_timer_ppi[PHYS_SECURE_PPI], 0);
		if (arch_timer_ppi[PHYS_NONSECURE_PPI])
			enable_percpu_irq(arch_timer_ppi[PHYS_NONSECURE_PPI], 0);
	}

	return 0;
}

/* Is the optional system timer available? */
static int local_timer_is_architected(void)
{
	return (cpu_architecture() >= CPU_ARCH_ARMv7) &&
	       ((read_cpuid_ext(CPUID_EXT_PFR1) >> 16) & 0xf) == 1;
}

static int arch_timer_available(void)
{
	unsigned long freq;

	if (!local_timer_is_architected())
		return -ENXIO;

	if (arch_timer_rate == 0) {
		freq = arch_timer_reg_read(ARCH_TIMER_PHYS_ACCESS,
					   ARCH_TIMER_REG_FREQ);

		/* Check the timer frequency. */
		if (freq == 0) {
			pr_warn("Architected timer frequency not available\n");
			return -EINVAL;
		}

		arch_timer_rate = freq;
	}

	pr_info_once("Architected local timer running at %lu.%02luMHz (%s).\n",
		     arch_timer_rate / 1000000, (arch_timer_rate / 10000) % 100,
		     arch_timer_use_virtual ? "virt" : "phys");
	return 0;
}

static u32 notrace arch_counter_get_cntpct32(void)
{
	cycle_t cnt = arch_counter_get_cntpct();

	/*
	 * The sched_clock infrastructure only knows about counters
	 * with at most 32bits. Forget about the upper 24 bits for the
	 * time being...
	 */
	return (u32)cnt;
}

static u32 notrace arch_counter_get_cntvct32(void)
{
	cycle_t cnt = arch_counter_get_cntvct();

	/*
	 * The sched_clock infrastructure only knows about counters
	 * with at most 32bits. Forget about the upper 24 bits for the
	 * time being...
	 */
	return (u32)cnt;
}

static cycle_t arch_counter_read(struct clocksource *cs)
{
	/*
	 * Always use the physical counter for the clocksource.
	 * CNTHCTL.PL1PCTEN must be set to 1.
	 */
	return arch_counter_get_cntpct();
}

static unsigned long arch_timer_read_current_timer(void)
=======
static unsigned long arch_timer_read_counter_long(void)
>>>>>>> v3.9
{
	return arch_timer_read_counter();
}

static u32 sched_clock_mult __read_mostly;

static unsigned long long notrace arch_timer_sched_clock(void)
{
	return arch_timer_read_counter() * sched_clock_mult;
}

static struct delay_timer arch_delay_timer;

static void __init arch_timer_delay_timer_register(void)
{
<<<<<<< HEAD
	int err;
	int ppi;

	err = arch_timer_available();
	if (err)
		goto out;

	arch_timer_evt = alloc_percpu(struct clock_event_device *);
	if (!arch_timer_evt) {
		err = -ENOMEM;
		goto out;
	}

#ifdef CONFIG_IPIPE
	tsc_info.freq = arch_timer_rate;
	__ipipe_tsc_register(&tsc_info);
#endif /* CONFIG_IPIPE */

	clocksource_register_hz(&clocksource_counter, arch_timer_rate);
	cyclecounter.mult = clocksource_counter.mult;
	cyclecounter.shift = clocksource_counter.shift;
	timecounter_init(&timecounter, &cyclecounter,
			 arch_counter_get_cntpct());

	if (arch_timer_use_virtual) {
		ppi = arch_timer_ppi[VIRT_PPI];
		err = request_percpu_irq(ppi, arch_timer_handler_virt,
					 "arch_timer", arch_timer_evt);
	} else {
		ppi = arch_timer_ppi[PHYS_SECURE_PPI];
		err = request_percpu_irq(ppi, arch_timer_handler_phys,
					 "arch_timer", arch_timer_evt);
		if (!err && arch_timer_ppi[PHYS_NONSECURE_PPI]) {
			ppi = arch_timer_ppi[PHYS_NONSECURE_PPI];
			err = request_percpu_irq(ppi, arch_timer_handler_phys,
						 "arch_timer", arch_timer_evt);
			if (err)
				free_percpu_irq(arch_timer_ppi[PHYS_SECURE_PPI],
						arch_timer_evt);
		}
	}

	if (err) {
		pr_err("arch_timer: can't register interrupt %d (%d)\n",
		       ppi, err);
		goto out_free;
	}

	err = local_timer_register(&arch_timer_ops);
	if (err) {
		/*
		 * We couldn't register as a local timer (could be
		 * because we're on a UP platform, or because some
		 * other local timer is already present...). Try as a
		 * global timer instead.
		 */
		arch_timer_global_evt.cpumask = cpumask_of(0);
		err = arch_timer_setup(&arch_timer_global_evt);
	}
	if (err)
		goto out_free_irq;

=======
>>>>>>> v3.9
	/* Use the architected timer for the delay loop. */
	arch_delay_timer.read_current_timer = arch_timer_read_counter_long;
	arch_delay_timer.freq = arch_timer_get_rate();
	register_current_timer_delay(&arch_delay_timer);
}

int __init arch_timer_arch_init(void)
{
        u32 arch_timer_rate = arch_timer_get_rate();

	if (arch_timer_rate == 0)
		return -ENXIO;

	arch_timer_delay_timer_register();

	/* Cache the sched_clock multiplier to save a divide in the hot path. */
	sched_clock_mult = NSEC_PER_SEC / arch_timer_rate;
	sched_clock_func = arch_timer_sched_clock;
	pr_info("sched_clock: ARM arch timer >56 bits at %ukHz, resolution %uns\n",
		arch_timer_rate / 1000, sched_clock_mult);

	return 0;
}
