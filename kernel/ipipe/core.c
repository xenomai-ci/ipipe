/* -*- linux-c -*-
 * linux/kernel/ipipe/core.c
 *
 * Copyright (C) 2002-2005 Philippe Gerum.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 * USA; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Architecture-independent I-PIPE core support.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kallsyms.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/tick.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#endif	/* CONFIG_PROC_FS */
#include <linux/ipipe_trace.h>
#include <linux/ipipe_tickdev.h>
#include <linux/irq.h>

struct ipipe_domain ipipe_root;
EXPORT_SYMBOL_GPL(ipipe_root);

struct ipipe_domain *ipipe_head_domain = &ipipe_root;
EXPORT_SYMBOL_GPL(ipipe_head_domain);

DEFINE_PER_CPU(struct ipipe_percpu_domain_data, ipipe_percpu_darray[2]) = {
	/* Root domain is stalled on each CPU at startup. */
	[IPIPE_ROOT_SLOT] = { .status = IPIPE_STALL_MASK }
};
EXPORT_PER_CPU_SYMBOL_GPL(ipipe_percpu_darray);

DEFINE_PER_CPU(struct ipipe_domain *, ipipe_percpu_domain) = {
	&ipipe_root
};
EXPORT_PER_CPU_SYMBOL_GPL(ipipe_percpu_domain);

/* Copy of root status during NMI */
DEFINE_PER_CPU(unsigned long, ipipe_nmi_saved_root);

DECLARE_PER_CPU(struct tick_device, tick_cpu_device);

static DEFINE_PER_CPU(struct ipipe_tick_device, ipipe_tick_cpu_device);

#ifdef CONFIG_SMP

#define IPIPE_CRITICAL_TIMEOUT	1000000

static cpumask_t __ipipe_cpu_sync_map;

static cpumask_t __ipipe_cpu_lock_map;

static cpumask_t __ipipe_cpu_pass_map;

static unsigned long __ipipe_critical_lock;

static IPIPE_DEFINE_SPINLOCK(__ipipe_cpu_barrier);

static atomic_t __ipipe_critical_count = ATOMIC_INIT(0);

static void (*__ipipe_cpu_sync) (void);

#else /* !CONFIG_SMP */

/*
 * Create an alias to the unique root status, so that arch-dep code
 * may get simple and easy access to this percpu variable.  We also
 * create an array of pointers to the percpu domain data; this tends
 * to produce a better code when reaching non-root domains. We make
 * sure that the early boot code would be able to dereference the
 * pointer to the root domain data safely by statically initializing
 * its value (local_irq*() routines depend on this).
 */
extern unsigned long __ipipe_root_status
__attribute__((alias(__stringify(ipipe_percpu_darray))));
EXPORT_SYMBOL_GPL(__ipipe_root_status);

/*
 * Set up the perdomain pointers for direct access to the percpu
 * domain data. This saves a costly multiply each time we need to
 * refer to the contents of the percpu domain data array.
 */
DEFINE_PER_CPU(struct ipipe_percpu_domain_data *, ipipe_percpu_daddr[2]) = {
	[IPIPE_ROOT_SLOT] = &ipipe_percpu_darray[0],
	[IPIPE_HEAD_SLOT] = &ipipe_percpu_darray[1],
};
EXPORT_PER_CPU_SYMBOL_GPL(ipipe_percpu_daddr);

#endif /* !CONFIG_SMP */

IPIPE_DEFINE_SPINLOCK(__ipipe_lock);

unsigned long __ipipe_virtual_irq_map;

#ifdef CONFIG_PRINTK
unsigned __ipipe_printk_virq;
int __ipipe_printk_bypass;
#endif /* CONFIG_PRINTK */

int __ipipe_event_monitors[IPIPE_NR_EVENTS];

#ifdef CONFIG_PROC_FS

struct proc_dir_entry *ipipe_proc_root;

static int __ipipe_version_info_proc(char *page,
				     char **start,
				     off_t off, int count, int *eof, void *data)
{
	int len = sprintf(page, "%s\n", IPIPE_VERSION_STRING);

	len -= off;

	if (len <= off + count)
		*eof = 1;

	*start = page + off;

	if(len > count)
		len = count;

	if(len < 0)
		len = 0;

	return len;
}

static int __ipipe_common_info_show(struct seq_file *p, void *data)
{
	struct ipipe_domain *ipd = (struct ipipe_domain *)p->private;
	char handling, stickiness, lockbit, exclusive, virtuality;

	unsigned long ctlbits;
	unsigned irq;

	seq_printf(p, "       +----- Handling ([A]ccepted, [G]rabbed, [W]ired, [D]iscarded)\n");
	seq_printf(p, "       |+---- Sticky\n");
	seq_printf(p, "       ||+--- Locked\n");
	seq_printf(p, "       |||+-- Exclusive\n");
	seq_printf(p, "       ||||+- Virtual\n");
	seq_printf(p, "[IRQ]  |||||\n");

	mutex_lock(&ipd->mutex);

	for (irq = 0; irq < IPIPE_NR_IRQS; irq++) {
		/* Remember to protect against
		 * ipipe_virtual_irq/ipipe_control_irq if more fields
		 * get involved. */
		ctlbits = ipd->irqs[irq].control;

		if (irq >= IPIPE_NR_XIRQS && !ipipe_virtual_irq_p(irq))
			/*
			 * There might be a hole between the last external
			 * IRQ and the first virtual one; skip it.
			 */
			continue;

		if (ipipe_virtual_irq_p(irq)
		    && !test_bit(irq - IPIPE_VIRQ_BASE, &__ipipe_virtual_irq_map))
			/* Non-allocated virtual IRQ; skip it. */
			continue;

		if (ctlbits & IPIPE_HANDLE_MASK)
			handling = 'G';
		else
			handling = 'D';

		if (ctlbits & IPIPE_STICKY_MASK)
			stickiness = 'S';
		else
			stickiness = '.';

		if (ctlbits & IPIPE_LOCK_MASK)
			lockbit = 'L';
		else
			lockbit = '.';

		if (ctlbits & IPIPE_EXCLUSIVE_MASK)
			exclusive = 'X';
		else
			exclusive = '.';

		if (ipipe_virtual_irq_p(irq))
			virtuality = 'V';
		else
			virtuality = '.';

		seq_printf(p, " %3u:  %c%c%c%c%c\n",
			     irq, handling, stickiness, lockbit, exclusive, virtuality);
	}

#ifdef CONFIG_IPIPE_LEGACY
	seq_printf(p, "[Domain info]\n");

	seq_printf(p, "id=0x%.8x\n", ipd->domid);

	if (ipd != ipipe_root_domain)
		seq_printf(p, "priority=topmost\n");
	else
		seq_printf(p, "priority=%d\n", ipd->priority);
#endif /* CONFIG_IPIPE_LEGACY */

	mutex_unlock(&ipd->mutex);

	return 0;
}

static int __ipipe_common_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, __ipipe_common_info_show, PROC_I(inode)->pde->data);
}

static struct file_operations __ipipe_info_proc_ops = {
	.owner		= THIS_MODULE,
	.open		= __ipipe_common_info_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void add_domain_proc(struct ipipe_domain *ipd)
{
	struct proc_dir_entry *e = create_proc_entry(ipd->name, 0444, ipipe_proc_root);
	if (e) {
		e->proc_fops = &__ipipe_info_proc_ops;
		e->data = (void*) ipd;
	}
}

void remove_domain_proc(struct ipipe_domain *ipd)
{
	remove_proc_entry(ipd->name,ipipe_proc_root);
}

void __init ipipe_init_proc(void)
{
	ipipe_proc_root = create_proc_entry("ipipe",S_IFDIR, 0);
	create_proc_read_entry("version",0444,ipipe_proc_root,&__ipipe_version_info_proc,NULL);
	add_domain_proc(ipipe_root_domain);

	__ipipe_init_tracer();
}

#else

static inline void add_domain_proc(struct ipipe_domain *ipd)
{
}

static inline void remove_domain_proc(struct ipipe_domain *ipd)
{
}

#endif	/* CONFIG_PROC_FS */

int ipipe_request_tickdev(const char *devname,
			  void (*emumode)(enum clock_event_mode mode,
					  struct clock_event_device *cdev),
			  int (*emutick)(unsigned long delta,
					 struct clock_event_device *cdev),
			  int cpu, unsigned long *tmfreq)
{
	struct ipipe_tick_device *itd;
	struct tick_device *slave;
	struct clock_event_device *evtdev;
	unsigned long long freq;
	unsigned long flags;
	int status;

	flags = ipipe_critical_enter(NULL);

	itd = &per_cpu(ipipe_tick_cpu_device, cpu);

	if (itd->slave != NULL) {
		status = -EBUSY;
		goto out;
	}

	slave = &per_cpu(tick_cpu_device, cpu);

	if (strcmp(slave->evtdev->name, devname)) {
		/*
		 * No conflict so far with the current tick device,
		 * check whether the requested device is sane and has
		 * been blessed by the kernel.
		 */
		status = __ipipe_check_tickdev(devname) ?
			CLOCK_EVT_MODE_UNUSED : CLOCK_EVT_MODE_SHUTDOWN;
		goto out;
	}

	/*
	 * Our caller asks for using the same clock event device for
	 * ticking than we do, let's create a tick emulation device to
	 * interpose on the set_next_event() method, so that we may
	 * both manage the device in oneshot mode. Only the tick
	 * emulation code will actually program the clockchip hardware
	 * for the next shot, though.
	 *
	 * CAUTION: we still have to grab the tick device even when it
	 * current runs in periodic mode, since the kernel may switch
	 * to oneshot dynamically (highres/no_hz tick mode).
	 */

	evtdev = slave->evtdev;
	status = evtdev->mode;

        if (status == CLOCK_EVT_MODE_SHUTDOWN)
                goto out;

	itd->slave = slave;
	itd->emul_set_mode = emumode;
	itd->emul_set_tick = emutick;
	itd->real_set_mode = evtdev->set_mode;
	itd->real_set_tick = evtdev->set_next_event;
	itd->real_max_delta_ns = evtdev->max_delta_ns;
	itd->real_mult = evtdev->mult;
	itd->real_shift = evtdev->shift;
	freq = (1000000000ULL * evtdev->mult) >> evtdev->shift;
	*tmfreq = (unsigned long)freq;
	evtdev->set_mode = emumode;
	evtdev->set_next_event = emutick;
	evtdev->max_delta_ns = ULONG_MAX;
	evtdev->mult = 1;
	evtdev->shift = 0;
out:
	ipipe_critical_exit(flags);

	return status;
}

void ipipe_release_tickdev(int cpu)
{
	struct ipipe_tick_device *itd;
	struct tick_device *slave;
	struct clock_event_device *evtdev;
	unsigned long flags;

	flags = ipipe_critical_enter(NULL);

	itd = &per_cpu(ipipe_tick_cpu_device, cpu);

	if (itd->slave != NULL) {
		slave = &per_cpu(tick_cpu_device, cpu);
		evtdev = slave->evtdev;
		evtdev->set_mode = itd->real_set_mode;
		evtdev->set_next_event = itd->real_set_tick;
		evtdev->max_delta_ns = itd->real_max_delta_ns;
		evtdev->mult = itd->real_mult;
		evtdev->shift = itd->real_shift;
		itd->slave = NULL;
	}

	ipipe_critical_exit(flags);
}

static void init_stage(struct ipipe_domain *ipd)
{
	struct ipipe_percpu_domain_data *p;
	unsigned long status;
	int cpu, n;

	for_each_online_cpu(cpu) {
		p = ipipe_percpudom_ptr(ipd, cpu);
		status = p->status;
		memset(p, 0, sizeof(*p));
		p->status = status;
	}

	for (n = 0; n < IPIPE_NR_IRQS; n++) {
		ipd->irqs[n].acknowledge = NULL;
		ipd->irqs[n].handler = NULL;
		ipd->irqs[n].control = 0;
	}

	for (n = 0; n < IPIPE_NR_EVENTS; n++)
		ipd->evhand[n] = NULL;

	ipd->evself = 0LL;
	mutex_init(&ipd->mutex);

	__ipipe_hook_critical_ipi(ipd);
}

void __init ipipe_init_early(void)
{
	struct ipipe_domain *ipd = &ipipe_root;

	/*
	 * Do the early init stuff. At this point, the kernel does not
	 * provide much services yet: be careful.
	 */
	__ipipe_check_platform(); /* Do platform dependent checks first. */

	/*
	 * A lightweight registration code for the root domain. We are
	 * running on the boot CPU, hw interrupts are off, and
	 * secondary CPUs are still lost in space.
	 */

	/* Reserve percpu data slot #0 for the root domain. */
	ipd->slot = IPIPE_ROOT_SLOT;
	ipd->name = "Linux";
#ifdef CONFIG_IPIPE_LEGACY
	ipd->domid = IPIPE_ROOT_ID;
	ipd->priority = IPIPE_ROOT_PRIO;
#endif
	init_stage(ipd);
	__ipipe_init_platform();

#ifdef CONFIG_PRINTK
	__ipipe_printk_virq = ipipe_alloc_virq();	/* Cannot fail here. */
	ipd->irqs[__ipipe_printk_virq].handler = &__ipipe_flush_printk;
	ipd->irqs[__ipipe_printk_virq].cookie = NULL;
	ipd->irqs[__ipipe_printk_virq].acknowledge = NULL;
	ipd->irqs[__ipipe_printk_virq].control = IPIPE_HANDLE_MASK;
#endif /* CONFIG_PRINTK */
}

void __init ipipe_init(void)
{
	/* Now we may engage the pipeline. */
	__ipipe_enable_pipeline();

	printk(KERN_INFO "I-pipe %s: pipeline enabled.\n",
	       IPIPE_VERSION_STRING);
}

void ipipe_register_head(struct ipipe_domain *ipd, const char *name)
{
	BUG_ON(!ipipe_root_domain_p || ipd == &ipipe_root);

	ipd->name = name;
	ipd->slot = IPIPE_HEAD_SLOT;
	init_stage(ipd);
	ipipe_head_domain = ipd;
	add_domain_proc(ipd);

	printk(KERN_INFO "I-pipe: head domain %s registered.\n", name);
}
EXPORT_SYMBOL_GPL(ipipe_register_head);

void ipipe_unregister_head(struct ipipe_domain *ipd)
{
	BUG_ON(!ipipe_root_domain_p || ipd != ipipe_head_domain);

	ipipe_head_domain = &ipipe_root;
	smp_mb();
	mutex_lock(&ipd->mutex);
	remove_domain_proc(ipd);
	mutex_unlock(&ipd->mutex);

	printk(KERN_INFO "I-pipe: head domain %s unregistered.\n", ipd->name);
}
EXPORT_SYMBOL_GPL(ipipe_unregister_head);

void __ipipe_unstall_root(void)
{
	struct ipipe_percpu_domain_data *p;

        local_irq_disable_hw();

	/* This helps catching bad usage from assembly call sites. */
	ipipe_root_only();

	p = ipipe_root_cpudom_ptr();

        __clear_bit(IPIPE_STALL_FLAG, &p->status);

        if (unlikely(__ipipe_ipending_p(p)))
                __ipipe_sync_stage();

        local_irq_enable_hw();
}
EXPORT_SYMBOL_GPL(__ipipe_unstall_root);

void __ipipe_restore_root(unsigned long x)
{
	ipipe_root_only();

	if (x)
		__ipipe_stall_root();
	else
		__ipipe_unstall_root();
}
EXPORT_SYMBOL_GPL(__ipipe_restore_root);

void ipipe_unstall_head(void)
{
	struct ipipe_percpu_domain_data *p = ipipe_head_cpudom_ptr();

	local_irq_disable_hw();

	__clear_bit(IPIPE_STALL_FLAG, &p->status);

	if (unlikely(__ipipe_ipending_p(p)))
		__ipipe_sync_pipeline(ipipe_head_domain);

	local_irq_enable_hw();
}
EXPORT_SYMBOL_GPL(ipipe_unstall_head);

void __ipipe_restore_head(unsigned long x) /* hw interrupt off */
{
	struct ipipe_percpu_domain_data *p = ipipe_head_cpudom_ptr();

	if (x) {
#ifdef CONFIG_DEBUG_KERNEL
		static int warned;
		if (!warned &&
		    __test_and_set_bit(IPIPE_STALL_FLAG, &p->status)) {
			/*
			 * Already stalled albeit ipipe_restore_head()
			 * should have detected it? Send a warning once.
			 */
			local_irq_enable_hw();	
			warned = 1;
			printk(KERN_WARNING
				   "I-pipe: ipipe_restore_head() optimization failed.\n");
			dump_stack();
			local_irq_disable_hw();	
		}
#else /* !CONFIG_DEBUG_KERNEL */
		__set_bit(IPIPE_STALL_FLAG, &p->status);
#endif /* CONFIG_DEBUG_KERNEL */
	} else {
		__clear_bit(IPIPE_STALL_FLAG, &p->status);
		if (unlikely(__ipipe_ipending_p(p)))
			__ipipe_sync_pipeline(ipipe_head_domain);
		local_irq_enable_hw();
	}
}
EXPORT_SYMBOL_GPL(__ipipe_restore_head);

void __ipipe_spin_lock_irq(ipipe_spinlock_t *lock)
{
	local_irq_disable_hw();
	arch_spin_lock(&lock->arch_lock);
	__set_bit(IPIPE_STALL_FLAG, &ipipe_this_cpudom_var(status));
}

void __ipipe_spin_unlock_irq(ipipe_spinlock_t *lock)
{
	arch_spin_unlock(&lock->arch_lock);
	__clear_bit(IPIPE_STALL_FLAG, &ipipe_this_cpudom_var(status));
	local_irq_enable_hw();
}

unsigned long __ipipe_spin_lock_irqsave(ipipe_spinlock_t *lock)
{
	unsigned long flags;
	int s;

	local_irq_save_hw(flags);
	arch_spin_lock(&lock->arch_lock);
	s = __test_and_set_bit(IPIPE_STALL_FLAG, &ipipe_this_cpudom_var(status));

	return arch_mangle_irq_bits(s, flags);
}

int __ipipe_spin_trylock_irqsave(ipipe_spinlock_t *lock,
				 unsigned long *x)
{
	unsigned long flags;
	int s;

	local_irq_save_hw(flags);
	if (!arch_spin_trylock(&lock->arch_lock)) {
		local_irq_restore_hw(flags);
		return 0;
	}
	s = __test_and_set_bit(IPIPE_STALL_FLAG, &ipipe_this_cpudom_var(status));
	*x = arch_mangle_irq_bits(s, flags);

	return 1;
}

void __ipipe_spin_unlock_irqrestore(ipipe_spinlock_t *lock,
				    unsigned long x)
{
	arch_spin_unlock(&lock->arch_lock);
	if (!arch_demangle_irq_bits(&x))
		__clear_bit(IPIPE_STALL_FLAG, &ipipe_this_cpudom_var(status));
	local_irq_restore_hw(x);
}

int __ipipe_spin_trylock_irq(ipipe_spinlock_t *lock)
{
	unsigned long flags;

	local_irq_save_hw(flags);
	if (!arch_spin_trylock(&lock->arch_lock)) {
		local_irq_restore_hw(flags);
		return 0;
	}
	__set_bit(IPIPE_STALL_FLAG, &ipipe_this_cpudom_var(status));

	return 1;
}

void __ipipe_spin_unlock_irqbegin(ipipe_spinlock_t *lock)
{
	arch_spin_unlock(&lock->arch_lock);
}

void __ipipe_spin_unlock_irqcomplete(unsigned long x)
{
	if (!arch_demangle_irq_bits(&x))
		__clear_bit(IPIPE_STALL_FLAG, &ipipe_this_cpudom_var(status));
	local_irq_restore_hw(x);
}

#ifdef __IPIPE_3LEVEL_IRQMAP

/* Must be called hw IRQs off. */
static inline void __ipipe_set_irq_held(struct ipipe_percpu_domain_data *p,
					unsigned int irq)
{
	__set_bit(irq, p->irqheld_map);
	p->irqall[irq]++;
}

/* Must be called hw IRQs off. */
void __ipipe_set_irq_pending(struct ipipe_domain *ipd, unsigned int irq)
{
	struct ipipe_percpu_domain_data *p = ipipe_cpudom_ptr(ipd);
	int l0b, l1b;

	IPIPE_WARN_ONCE(!irqs_disabled_hw());

	l0b = irq / (BITS_PER_LONG * BITS_PER_LONG);
	l1b = irq / BITS_PER_LONG;

	if (likely(!test_bit(IPIPE_LOCK_FLAG, &ipd->irqs[irq].control))) {
		__set_bit(irq, p->irqpend_lomap);
		__set_bit(l1b, p->irqpend_mdmap);
		__set_bit(l0b, &p->irqpend_himap);
	} else
		__set_bit(irq, p->irqheld_map);

	p->irqall[irq]++;
}
EXPORT_SYMBOL_GPL(__ipipe_set_irq_pending);

/* Must be called hw IRQs off. */
void __ipipe_lock_irq(unsigned int irq)
{
	struct ipipe_domain *ipd = ipipe_root_domain;
	struct ipipe_percpu_domain_data *p;
	int l0b, l1b;

	IPIPE_WARN_ONCE(!irqs_disabled_hw());

	/*
	 * Interrupts requested by a registered head domain cannot be
	 * locked, since this would make no sense: interrupts are
	 * globally masked at CPU level when the head domain is
	 * stalled, so there is no way we could encounter the
	 * situation IRQ locks are handling.
	 */
	if (test_and_set_bit(IPIPE_LOCK_FLAG, &ipd->irqs[irq].control))
		return;

	l0b = irq / (BITS_PER_LONG * BITS_PER_LONG);
	l1b = irq / BITS_PER_LONG;

	p = ipipe_cpudom_ptr(ipd);
	if (__test_and_clear_bit(irq, p->irqpend_lomap)) {
		__set_bit(irq, p->irqheld_map);
		if (p->irqpend_lomap[l1b] == 0) {
			__clear_bit(l1b, p->irqpend_mdmap);
			if (p->irqpend_mdmap[l0b] == 0)
				__clear_bit(l0b, &p->irqpend_himap);
		}
	}
}
EXPORT_SYMBOL_GPL(__ipipe_lock_irq);

/* Must be called hw IRQs off. */
void __ipipe_unlock_irq(unsigned int irq)
{
	struct ipipe_domain *ipd = ipipe_root_domain;
	struct ipipe_percpu_domain_data *p;
	int l0b, l1b, cpu;

	IPIPE_WARN_ONCE(!irqs_disabled_hw());

	if (!test_and_clear_bit(IPIPE_LOCK_FLAG, &ipd->irqs[irq].control))
		return;

	l0b = irq / (BITS_PER_LONG * BITS_PER_LONG);
	l1b = irq / BITS_PER_LONG;

	for_each_online_cpu(cpu) {
		p = ipipe_percpudom_ptr(ipd, cpu);
		if (test_and_clear_bit(irq, p->irqheld_map)) {
			/* We need atomic ops here: */
			set_bit(irq, p->irqpend_lomap);
			set_bit(l1b, p->irqpend_mdmap);
			set_bit(l0b, &p->irqpend_himap);
		}
	}
}
EXPORT_SYMBOL_GPL(__ipipe_unlock_irq);

static inline int __ipipe_next_irq(struct ipipe_percpu_domain_data *p)
{
	int l0b, l1b, l2b;
	unsigned long l0m, l1m, l2m;
	unsigned int irq;

	l0m = p->irqpend_himap;
	if (unlikely(l0m == 0))
		return -1;

	l0b = __ipipe_ffnz(l0m);
	l1m = p->irqpend_mdmap[l0b];
	if (unlikely(l1m == 0))
		return -1;

	l1b = __ipipe_ffnz(l1m) + l0b * BITS_PER_LONG;
	l2m = p->irqpend_lomap[l1b];
	if (unlikely(l2m == 0))
		return -1;

	l2b = __ipipe_ffnz(l2m);
	irq = l1b * BITS_PER_LONG + l2b;

	__clear_bit(irq, p->irqpend_lomap);
	if (p->irqpend_lomap[l1b] == 0) {
		__clear_bit(l1b, p->irqpend_mdmap);
		if (p->irqpend_mdmap[l0b] == 0)
			__clear_bit(l0b, &p->irqpend_himap);
	}

	return irq;
}

#else /* __IPIPE_2LEVEL_IRQMAP */

/* Must be called hw IRQs off. */
static inline void __ipipe_set_irq_held(struct ipipe_percpu_domain_data *p,
					unsigned int irq)
{
	__set_bit(irq, p->irqheld_map);
	p->irqall[irq]++;
}

/* Must be called hw IRQs off. */
void __ipipe_set_irq_pending(struct ipipe_domain *ipd, unsigned int irq)
{
	struct ipipe_percpu_domain_data *p = ipipe_cpudom_ptr(ipd);
	int l0b = irq / BITS_PER_LONG;

	IPIPE_WARN_ONCE(!irqs_disabled_hw());
	
	if (likely(!test_bit(IPIPE_LOCK_FLAG, &ipd->irqs[irq].control))) {
		__set_bit(irq, p->irqpend_lomap);
		__set_bit(l0b, &p->irqpend_himap);
	} else
		__set_bit(irq, p->irqheld_map);

	p->irqall[irq]++;
}
EXPORT_SYMBOL_GPL(__ipipe_set_irq_pending);

/* Must be called hw IRQs off. */
void __ipipe_lock_irq(unsigned int irq)
{
	struct ipipe_domain *ipd = ipipe_root_domain;
	struct ipipe_percpu_domain_data *p;
	int l0b = irq / BITS_PER_LONG;

	IPIPE_WARN_ONCE(!irqs_disabled_hw());

	if (test_and_set_bit(IPIPE_LOCK_FLAG, &ipd->irqs[irq].control))
		return;

	p = ipipe_cpudom_ptr(ipd);
	if (__test_and_clear_bit(irq, p->irqpend_lomap)) {
		__set_bit(irq, p->irqheld_map);
		if (p->irqpend_lomap[l0b] == 0)
			__clear_bit(l0b, &p->irqpend_himap);
	}
}
EXPORT_SYMBOL_GPL(__ipipe_lock_irq);

/* Must be called hw IRQs off. */
void __ipipe_unlock_irq(unsigned int irq)
{
	struct ipipe_domain *ipd = ipipe_root_domain;
	struct ipipe_percpu_domain_data *p;
	int l0b = irq / BITS_PER_LONG, cpu;

	IPIPE_WARN_ONCE(!irqs_disabled_hw());

	if (!test_and_clear_bit(IPIPE_LOCK_FLAG, &ipd->irqs[irq].control))
		return;

	for_each_online_cpu(cpu) {
		p = ipipe_percpudom_ptr(ipd, cpu);
		if (test_and_clear_bit(irq, p->irqheld_map)) {
			/* We need atomic ops here: */
			set_bit(irq, p->irqpend_lomap);
			set_bit(l0b, &p->irqpend_himap);
		}
	}
}
EXPORT_SYMBOL_GPL(__ipipe_unlock_irq);

static inline int __ipipe_next_irq(struct ipipe_percpu_domain_data *p)
{
	unsigned long l0m, l1m;
	int l0b, l1b;

	l0m = p->irqpend_himap;
	if (unlikely(l0m == 0))
		return -1;

	l0b = __ipipe_ffnz(l0m);
	l1m = p->irqpend_lomap[l0b];
	if (unlikely(l1m == 0))
		return -1;

	l1b = __ipipe_ffnz(l1m);
	__clear_bit(l1b, &p->irqpend_lomap[l0b]);
	if (p->irqpend_lomap[l0b] == 0)
		__clear_bit(l0b, &p->irqpend_himap);

	return l0b * BITS_PER_LONG + l1b;
}

#endif /* __IPIPE_2LEVEL_IRQMAP */

void __ipipe_do_sync_pipeline(struct ipipe_domain *top)
{
	struct ipipe_percpu_domain_data *p;
	struct ipipe_domain *ipd;

	/* We must enter over the root domain. */
	IPIPE_WARN_ONCE(__ipipe_current_domain != ipipe_root_domain);
	ipd = top;
next:
	p = ipipe_cpudom_ptr(ipd);
	if (test_bit(IPIPE_STALL_FLAG, &p->status))
		return;

	if (__ipipe_ipending_p(p)) {
		if (ipd == ipipe_root_domain)
			__ipipe_sync_stage();
		else {
			/* Switching to head. */
			p->evsync = 0;
			__ipipe_current_domain = ipd;
			__ipipe_sync_stage();
			__ipipe_current_domain = ipipe_root_domain;
		}
	}

	if (ipd != ipipe_root_domain) {
		ipd = ipipe_root_domain;
		goto next;
	}
}
EXPORT_SYMBOL_GPL(__ipipe_do_sync_pipeline);

void ipipe_suspend_domain(void)
{
	struct ipipe_percpu_domain_data *p;
	struct ipipe_domain *ipd;
	unsigned long flags;

	local_irq_save_hw(flags);

	ipd = __ipipe_current_domain;
	p = ipipe_cpudom_ptr(ipd);
	clear_bit(IPIPE_STALL_FLAG, &p->status);
	if (__ipipe_ipending_p(p))
		__ipipe_sync_stage();

	if (ipd != ipipe_root_domain) {
		ipd = ipipe_root_domain;
		p = ipipe_cpudom_ptr(ipd);
		__ipipe_current_domain = ipd;
		if (!test_bit(IPIPE_STALL_FLAG, &p->status) &&
		    __ipipe_ipending_p(p))
			__ipipe_sync_stage();
	}

	local_irq_restore_hw(flags);
}
EXPORT_SYMBOL_GPL(ipipe_suspend_domain);


/* ipipe_alloc_virq() -- Allocate a pipelined virtual/soft interrupt.
 * Virtual interrupts are handled in exactly the same way than their
 * hw-generated counterparts wrt pipelining.
 */
unsigned ipipe_alloc_virq(void)
{
	unsigned long flags, irq = 0;
	int ipos;

	spin_lock_irqsave(&__ipipe_lock, flags);

	if (__ipipe_virtual_irq_map != ~0) {
		ipos = ffz(__ipipe_virtual_irq_map);
		set_bit(ipos, &__ipipe_virtual_irq_map);
		irq = ipos + IPIPE_VIRQ_BASE;
	}

	spin_unlock_irqrestore(&__ipipe_lock, flags);

	return irq;
}

int ipipe_virtualize_irq(struct ipipe_domain *ipd,
			 unsigned int irq,
			 ipipe_irq_handler_t handler,
			 void *cookie,
			 ipipe_irq_ackfn_t acknowledge,
			 unsigned modemask)
{
	ipipe_irq_handler_t old_handler;
	struct irq_desc *desc;
	unsigned long flags;
	int ret = 0;

	if (irq >= IPIPE_NR_IRQS)
		return -EINVAL;

	if (ipd->irqs[irq].control & IPIPE_SYSTEM_MASK)
		return -EPERM;

	spin_lock_irqsave(&__ipipe_lock, flags);

	old_handler = ipd->irqs[irq].handler;

	if (handler == NULL) {
		modemask &=
		    ~(IPIPE_HANDLE_MASK | IPIPE_STICKY_MASK |
		      IPIPE_EXCLUSIVE_MASK);

		ipd->irqs[irq].handler = NULL;
		ipd->irqs[irq].cookie = NULL;
		ipd->irqs[irq].acknowledge = NULL;
		ipd->irqs[irq].control = modemask;

		if (irq < NR_IRQS && !ipipe_virtual_irq_p(irq)) {
			desc = irq_to_desc(irq);
			if (old_handler && desc)
				__ipipe_disable_irqdesc(ipd, irq);
		}

		goto unlock_and_exit;
	}

	if (handler == IPIPE_SAME_HANDLER) {
		cookie = ipd->irqs[irq].cookie;
		handler = old_handler;
		if (handler == NULL) {
			ret = -EINVAL;
			goto unlock_and_exit;
		}
	} else if ((modemask & IPIPE_EXCLUSIVE_MASK) != 0 && old_handler) {
		ret = -EBUSY;
		goto unlock_and_exit;
	}

	if ((modemask & IPIPE_STICKY_MASK) != 0)
		modemask |= IPIPE_HANDLE_MASK;

	if (acknowledge == NULL)
		/*
		 * Acknowledge handler unspecified for a hw interrupt:
		 * use the Linux-defined handler instead.
		 */
		acknowledge = ipipe_root_domain->irqs[irq].acknowledge;

	ipd->irqs[irq].handler = handler;
	ipd->irqs[irq].cookie = cookie;
	ipd->irqs[irq].acknowledge = acknowledge;
	ipd->irqs[irq].control = modemask;

	desc = irq_to_desc(irq);
	if (desc == NULL)
		goto unlock_and_exit;

	if (irq < NR_IRQS && !ipipe_virtual_irq_p(irq)) {
		__ipipe_enable_irqdesc(ipd, irq);
		/*
		 * IRQ enable/disable state is domain-sensitive, so we
		 * may not change it for another domain. What is
		 * allowed however is forcing some domain to handle an
		 * interrupt source, by passing the proper 'ipd'
		 * descriptor which thus may be different from
		 * __ipipe_current_domain.
		 */
		if ((modemask & IPIPE_ENABLE_MASK) != 0) {
			if (ipd != __ipipe_current_domain)
				ret = -EPERM;
			else
				__ipipe_enable_irq(irq);
		}
	}

unlock_and_exit:

	spin_unlock_irqrestore(&__ipipe_lock, flags);

	return ret;
}

int __ipipe_dispatch_event(unsigned int event, void *data)
{
	struct ipipe_domain *caller_domain, *this_domain, *ipd;
	struct ipipe_percpu_domain_data *p;
	ipipe_event_handler_t handler;
	unsigned long flags;
	int ret = 0;

	caller_domain = this_domain = __ipipe_current_domain;
	ipd = ipipe_head_domain;
	local_irq_save_hw(flags);
next:
	p = ipipe_cpudom_ptr(ipd);
	handler = ipd->evhand[event];
	if (handler) {
		__ipipe_current_domain = ipd;
		p->evsync |= (1LL << event);
		local_irq_restore_hw(flags);
		ret = handler(event, caller_domain, data);
		local_irq_save_hw(flags);
		p->evsync &= ~(1LL << event);
		if (__ipipe_current_domain != ipd)
			/* Account for domain migration. */
			this_domain = __ipipe_current_domain;
		else
			__ipipe_current_domain = this_domain;
	}

	if (this_domain == ipipe_root_domain &&
	    ipd != ipipe_root_domain && ret == 0) {
		ipd = ipipe_root_domain;
		goto next;
	}

	local_irq_restore_hw(flags);

	return ret;
}

void __ipipe_dispatch_irq(unsigned int irq, int ackit) /* hw interrupts off */
{
	struct ipipe_domain *ipd;
	struct irq_desc *desc;
	unsigned long control;

	/*
	 * Survival kit when reading this code:
	 *
	 * - we have two main situations, leading to three cases for
	 *   handling interrupts:
	 *
	 *   a) the root domain is alone, no registered head domain
	 *      => all interrupts are delivered via the fast dispatcher.
	 *   b) a head domain is registered
	 *      => head domain IRQs go through the fast dispatcher
	 *      => root domain IRQs go through the interrupt log
	 *
	 * - when no head domain is registered, ipipe_head_domain ==
	 *   ipipe_root_domain == &ipipe_root.
	 *
	 * - the caller tells us whether we should acknowledge this
	 *   IRQ. Even virtual IRQs may require acknowledge on some
	 *   platforms (e.g. arm/SMP).
	 */

#ifdef CONFIG_IPIPE_DEBUG
	if (unlikely(irq >= IPIPE_NR_IRQS) ||
	    (!ipipe_virtual_irq_p(irq) && irq_to_desc(irq) == NULL)) {
		printk(KERN_ERR "I-pipe: spurious interrupt %u\n", irq);
		return;
	}
#endif
	/*
	 * CAUTION: on some archs, virtual IRQs may have acknowledge
	 * handlers.
	 */
	if (likely(ackit)) {
		ipd = ipipe_head_domain;
		control = ipd->irqs[irq].control;
		if ((control & IPIPE_HANDLE_MASK) == 0)
			ipd = ipipe_root_domain;
		desc = ipipe_virtual_irq_p(irq) ? NULL : irq_to_desc(irq);
		if (ipd->irqs[irq].acknowledge)
			ipd->irqs[irq].acknowledge(irq, desc);
	}

	/*
	 * Sticky interrupts must be handled early and separately, so
	 * that we always process them on the current domain.
	 */
	ipd = __ipipe_current_domain;
	control = ipd->irqs[irq].control;
	if (control & IPIPE_STICKY_MASK)
		goto log;

	/*
	 * In case we have no registered head domain
	 * (i.e. ipipe_head_domain == &ipipe_root), we allow
	 * interrupts to go through the fast dispatcher, since we
	 * don't care for the latency induced by interrupt disabling
	 * at CPU level. Otherwise, we must go through the interrupt
	 * log, and leave the dispatching work ultimately to
	 * __ipipe_do_sync_stage().
	 */
	ipd = ipipe_head_domain;
	control = ipd->irqs[irq].control;
	if (control & IPIPE_HANDLE_MASK) {
		__ipipe_dispatch_irq_fast(irq);
		return;
	}

	/*
	 * The root domain must handle all interrupts, so testing the
	 * HANDLE bit for it would be pointless.
	 */
	ipd = ipipe_root_domain;
log:
	__ipipe_set_irq_pending(ipd, irq);

	/*
	 * Optimize if we preempted the high priority head domain (not
	 * the root one in absence of ipipe_register_head()): we don't
	 * need to synchronize the pipeline unless there is a pending
	 * interrupt for it.
	 */
	if (__ipipe_current_domain != ipipe_root_domain &&
	    !__ipipe_ipending_p(ipipe_head_cpudom_ptr()))
		return;

	__ipipe_sync_pipeline(ipipe_head_domain);
}

void __ipipe_dispatch_irq_fast_nocheck(unsigned int irq) /* hw interrupts off */
{
	struct ipipe_percpu_domain_data *p = ipipe_cpudom_ptr(ipipe_head_domain);
	struct ipipe_domain *head = ipipe_head_domain;
	struct ipipe_domain *old;

	old = __ipipe_current_domain;
	/* Switch to the head domain. */
	__ipipe_current_domain = head;

	p->irqall[irq]++;
	__set_bit(IPIPE_STALL_FLAG, &p->status);
	barrier();
	head->irqs[irq].handler(irq, head->irqs[irq].cookie);
	__ipipe_run_irqtail(irq);
	barrier();
	__clear_bit(IPIPE_STALL_FLAG, &p->status);

	if (__ipipe_current_domain == head) {
		__ipipe_current_domain = old;
		if (old == head) {
			if (__ipipe_ipending_p(p))
				__ipipe_sync_stage();
			return;
		}
	}

	/*
	 * We must be running over the root domain, synchronize
	 * the pipeline for high priority IRQs.
	 */
	__ipipe_do_sync_pipeline(head);
}

#ifdef CONFIG_TRACE_IRQFLAGS
#define root_stall_after_handler()	local_irq_disable()
#else
#define root_stall_after_handler()	do { } while (0)
#endif

#ifdef CONFIG_PREEMPT

asmlinkage void preempt_schedule_irq(void);

void __ipipe_preempt_schedule_irq(void)
{
	struct ipipe_percpu_domain_data *p; 
	unsigned long flags;  

	BUG_ON(!irqs_disabled_hw());
	local_irq_save(flags);
	local_irq_enable_hw();
	preempt_schedule_irq(); /* Ok, may reschedule now. */  
	local_irq_disable_hw();

	/*
	 * Flush any pending interrupt that may have been logged after
	 * preempt_schedule_irq() stalled the root stage before
	 * returning to us, and now.
	 */
	p = ipipe_root_cpudom_ptr();
	if (unlikely(__ipipe_ipending_p(p))) {
		add_preempt_count(PREEMPT_ACTIVE);
		trace_hardirqs_on();
		__clear_bit(IPIPE_STALL_FLAG, &p->status);
		__ipipe_sync_stage();
		sub_preempt_count(PREEMPT_ACTIVE);
	}

	__local_irq_restore_nosync(flags);
}

#else /* !CONFIG_PREEMPT */

#define __ipipe_preempt_schedule_irq()	do { } while (0)

#endif	/* !CONFIG_PREEMPT */

/*
 * __ipipe_do_sync_stage() -- Flush the pending IRQs for the current
 * domain (and processor). This routine flushes the interrupt log (see
 * "Optimistic interrupt protection" from D. Stodolsky et al. for more
 * on the deferred interrupt scheme). Every interrupt that occurred
 * while the pipeline was stalled gets played.
 *
 * WARNING: CPU migration may occur over this routine.
 */
void __ipipe_do_sync_stage(void)
{
	struct ipipe_percpu_domain_data *p;
	struct ipipe_domain *ipd;
	int irq;

	ipd = __ipipe_current_domain;
	p = ipipe_cpudom_ptr(ipd);

	__set_bit(IPIPE_STALL_FLAG, &p->status);
	smp_wmb();

	if (ipd == ipipe_root_domain)
		trace_hardirqs_off();

	for (;;) {
		irq = __ipipe_next_irq(p);
		if (irq < 0)
			break;
		/*
		 * Make sure the compiler does not reorder wrongly, so
		 * that all updates to maps are done before the
		 * handler gets called.
		 */
		barrier();

		if (test_bit(IPIPE_LOCK_FLAG, &ipd->irqs[irq].control))
			continue;

		if (ipd != ipipe_head_domain)
			local_irq_enable_hw();

		if (likely(ipd != ipipe_root_domain)) {
			ipd->irqs[irq].handler(irq, ipd->irqs[irq].cookie);
			__ipipe_run_irqtail(irq);
			local_irq_disable_hw();
		} else if (ipipe_virtual_irq_p(irq)) {
			irq_enter();
			ipd->irqs[irq].handler(irq, ipd->irqs[irq].cookie);
			irq_exit();
			root_stall_after_handler();
			local_irq_disable_hw();
			while (__ipipe_check_root_resched())
				__ipipe_preempt_schedule_irq();
		} else {
			__ipipe_do_root_xirq(ipd, irq);
			root_stall_after_handler();
			local_irq_disable_hw();
		}

		p = ipipe_cpudom_ptr(__ipipe_current_domain);
	}

	if (ipd == ipipe_root_domain)
		trace_hardirqs_on();

	__clear_bit(IPIPE_STALL_FLAG, &p->status);
}

void __ipipe_pend_irq(struct ipipe_domain *ipd, unsigned int irq)
{
#ifdef CONFIG_IPIPE_DEBUG
	BUG_ON(irq >= IPIPE_NR_IRQS ||
	       (ipipe_virtual_irq_p(irq)
		&& !test_bit(irq - IPIPE_VIRQ_BASE, &__ipipe_virtual_irq_map)));
#endif
	if (test_bit(IPIPE_HANDLE_FLAG, &ipd->irqs[irq].control)) {
		__ipipe_set_irq_pending(ipd, irq);
		return;
	}
	if (ipd != ipipe_root_domain) {
		ipd = ipipe_root_domain;
		if (test_bit(IPIPE_HANDLE_FLAG, &ipd->irqs[irq].control))
			__ipipe_set_irq_pending(ipd, irq);
	}
}
EXPORT_SYMBOL_GPL(__ipipe_pend_irq);

/* ipipe_free_virq() -- Release a virtual/soft interrupt. */

int ipipe_free_virq(unsigned virq)
{
	if (!ipipe_virtual_irq_p(virq))
		return -EINVAL;

	clear_bit(virq - IPIPE_VIRQ_BASE, &__ipipe_virtual_irq_map);

	return 0;
}

/*
 * ipipe_catch_event() -- Interpose or remove an event handler for a
 * given domain.
 */
ipipe_event_handler_t ipipe_catch_event(struct ipipe_domain *ipd,
					unsigned event,
					ipipe_event_handler_t handler)
{
	ipipe_event_handler_t old_handler;
	unsigned long flags;
	int self = 0, cpu;

	if (event & IPIPE_EVENT_SELF) {
		event &= ~IPIPE_EVENT_SELF;
		self = 1;
	}

	if (event >= IPIPE_NR_EVENTS)
		return NULL;

	flags = ipipe_critical_enter(NULL);

	if (!(old_handler = xchg(&ipd->evhand[event],handler)))	{
		if (handler) {
			if (self)
				ipd->evself |= (1LL << event);
			else
				__ipipe_event_monitors[event]++;
		}
	}
	else if (!handler) {
		if (ipd->evself & (1LL << event))
			ipd->evself &= ~(1LL << event);
		else
			__ipipe_event_monitors[event]--;
	} else if ((ipd->evself & (1LL << event)) && !self) {
			__ipipe_event_monitors[event]++;
			ipd->evself &= ~(1LL << event);
	} else if (!(ipd->evself & (1LL << event)) && self) {
			__ipipe_event_monitors[event]--;
			ipd->evself |= (1LL << event);
	}

	ipipe_critical_exit(flags);

	if (!handler && ipipe_root_domain_p) {
		/*
		 * If we cleared a handler on behalf of the root
		 * domain, we have to wait for any current invocation
		 * to drain, since our caller might subsequently unmap
		 * the target domain. To this aim, this code
		 * synchronizes with __ipipe_dispatch_event(),
		 * guaranteeing that either the dispatcher sees a null
		 * handler in which case it discards the invocation
		 * (which also prevents from entering a livelock), or
		 * finds a valid handler and calls it. Symmetrically,
		 * ipipe_catch_event() ensures that the called code
		 * won't be unmapped under our feet until the event
		 * synchronization flag is cleared for the given event
		 * on all CPUs.
		 */
		preempt_disable();
		cpu = smp_processor_id();
		/*
		 * Hack: this solves the potential migration issue
		 * raised in __ipipe_dispatch_event(). This is a
		 * work-around which makes the assumption that other
		 * CPUs will subsequently, either process at least one
		 * interrupt for the target domain, or call
		 * __ipipe_dispatch_event() without going through a
		 * migration while running the handler at least once;
		 * practically, this is safe on any normally running
		 * system.
		 */
		ipipe_percpudom(ipd, evsync, cpu) &= ~(1LL << event);
		preempt_enable();

		for_each_online_cpu(cpu) {
			while (ipipe_percpudom(ipd, evsync, cpu) & (1LL << event))
				schedule_timeout_interruptible(HZ / 50);
		}
	}

	return old_handler;
}

int ipipe_set_irq_affinity (unsigned irq, cpumask_t cpumask)
{
#ifdef CONFIG_SMP
	if (irq >= IPIPE_NR_XIRQS)
		/* Allow changing affinity of external IRQs only. */
		return -EINVAL;

	if (num_online_cpus() > 1)
		return __ipipe_set_irq_affinity(irq,cpumask);
#endif /* CONFIG_SMP */

	return 0;
}

int ipipe_send_ipi (unsigned ipi, cpumask_t cpumask)

{
#ifdef CONFIG_SMP
	if (!ipipe_ipi_p(ipi))
		return -EINVAL;
	return __ipipe_send_ipi(ipi,cpumask);
#else /* !CONFIG_SMP */
	return -EINVAL;
#endif /* CONFIG_SMP */
}

#ifdef CONFIG_SMP

/* Always called with hw interrupts off. */
void __ipipe_do_critical_sync(unsigned irq, void *cookie)
{
	int cpu = ipipe_processor_id();

	cpu_set(cpu, __ipipe_cpu_sync_map);

	/* Now we are in sync with the lock requestor running on another
	   CPU. Enter a spinning wait until he releases the global
	   lock. */
	spin_lock(&__ipipe_cpu_barrier);

	/* Got it. Now get out. */

	if (__ipipe_cpu_sync)
		/* Call the sync routine if any. */
		__ipipe_cpu_sync();

	cpu_set(cpu, __ipipe_cpu_pass_map);

	spin_unlock(&__ipipe_cpu_barrier);

	cpu_clear(cpu, __ipipe_cpu_sync_map);
}
#endif	/* CONFIG_SMP */

/*
 * ipipe_critical_enter() -- Grab the superlock excluding all CPUs but
 * the current one from a critical section. This lock is used when we
 * must enforce a global critical section for a single CPU in a
 * possibly SMP system whichever context the CPUs are running.
 */
unsigned long ipipe_critical_enter(void (*syncfn)(void))
{
	unsigned long flags;

	local_irq_save_hw(flags);

#ifdef CONFIG_SMP
	if (num_online_cpus() > 1) {
		int cpu = ipipe_processor_id();
		cpumask_t allbutself;
		unsigned long loops;

		if (!cpu_test_and_set(cpu, __ipipe_cpu_lock_map)) {
			while (test_and_set_bit(0, &__ipipe_critical_lock)) {
				int n = 0;

				local_irq_enable_hw();

				do {
					cpu_relax();
				} while (++n < cpu);

				local_irq_disable_hw();
			}

restart:
			spin_lock(&__ipipe_cpu_barrier);

			__ipipe_cpu_sync = syncfn;

			cpus_clear(__ipipe_cpu_pass_map);
			cpu_set(cpu, __ipipe_cpu_pass_map);

			/*
			 * Send the sync IPI to all processors but the current
			 * one.
			 */
			cpus_andnot(allbutself, cpu_online_map,
				    __ipipe_cpu_pass_map);
			__ipipe_send_ipi(IPIPE_CRITICAL_IPI, allbutself);

			loops = IPIPE_CRITICAL_TIMEOUT;

			while (!cpus_equal(__ipipe_cpu_sync_map, allbutself)) {
				cpu_relax();

				if (--loops == 0) {
					/*
					 * We ran into a deadlock due to a
					 * contended rwlock. Cancel this round
					 * and retry.
					 */
					__ipipe_cpu_sync = NULL;

					spin_unlock(&__ipipe_cpu_barrier);

					/*
					 * Ensure all CPUs consumed the IPI to
					 * avoid running __ipipe_cpu_sync
					 * prematurely. This usually resolves
					 * the deadlock reason too.
					 */
					while (!cpus_equal(cpu_online_map,
							   __ipipe_cpu_pass_map))
						cpu_relax();

					goto restart;
				}
			}
		}

		atomic_inc(&__ipipe_critical_count);
	}
#endif	/* CONFIG_SMP */

	return flags;
}

/* ipipe_critical_exit() -- Release the superlock. */

void ipipe_critical_exit(unsigned long flags)
{
#ifdef CONFIG_SMP
	if (num_online_cpus() > 1 &&
	    atomic_dec_and_test(&__ipipe_critical_count)) {
		spin_unlock(&__ipipe_cpu_barrier);

		while (!cpus_empty(__ipipe_cpu_sync_map))
			cpu_relax();

		cpu_clear(ipipe_processor_id(), __ipipe_cpu_lock_map);
		clear_bit(0, &__ipipe_critical_lock);
		smp_mb__after_clear_bit();
	}
#endif	/* CONFIG_SMP */

	local_irq_restore_hw(flags);
}

#ifdef CONFIG_HAVE_IPIPE_HOSTRT
/*
 * NOTE: The architecture specific code must only call this function
 * when a clocksource suitable for CLOCK_HOST_REALTIME is enabled.
 */
void ipipe_update_hostrt(struct timespec *wall_time, struct clocksource *clock)
{
	struct ipipe_hostrt_data hostrt_data;

	hostrt_data.live = 1;
	hostrt_data.cycle_last = clock->cycle_last;
	hostrt_data.mask = clock->mask;
	hostrt_data.mult = clock->mult;
	hostrt_data.shift = clock->shift;
	hostrt_data.wall_time_sec = wall_time->tv_sec;
	hostrt_data.wall_time_nsec = wall_time->tv_nsec;
	hostrt_data.wall_to_monotonic = __get_wall_to_monotonic();

	/* Note: The event receiver is responsible for providing
	   proper locking */
	if (__ipipe_event_monitored_p(IPIPE_EVENT_HOSTRT))
		__ipipe_dispatch_event(IPIPE_EVENT_HOSTRT, &hostrt_data);
}
#endif /* CONFIG_HAVE_IPIPE_HOSTRT */

#ifdef CONFIG_IPIPE_DEBUG_CONTEXT

DEFINE_PER_CPU(int, ipipe_percpu_context_check) = { 1 };
DEFINE_PER_CPU(int, ipipe_saved_context_check_state);

void ipipe_root_only(void)
{
        struct ipipe_percpu_domain_data *p; 
        struct ipipe_domain *this_domain; 
        unsigned long flags;
	int cpu;

        local_irq_save_hw_smp(flags); 

        this_domain = __ipipe_current_domain; 
        p = ipipe_head_cpudom_ptr(); 
        if (likely(this_domain == ipipe_root_domain && 
		   !test_bit(IPIPE_STALL_FLAG, &p->status))) { 
                local_irq_restore_hw_smp(flags); 
                return; 
        } 
 
	cpu = ipipe_processor_id();
        if (!per_cpu(ipipe_percpu_context_check, cpu)) { 
                local_irq_restore_hw_smp(flags); 
                return; 
        } 
 
        local_irq_restore_hw_smp(flags); 

	ipipe_context_check_off();
	ipipe_trace_panic_freeze();
	ipipe_set_printk_sync(__ipipe_current_domain);

	if (this_domain != ipipe_root_domain)
		printk(KERN_ERR
		       "I-pipe: Detected illicit call from head domain '%s'\n"
		       KERN_ERR "        into a regular Linux service\n",
		       this_domain->name);
	else
		printk(KERN_ERR "I-pipe: Detected stalled head domain, "
				"probably caused by a bug.\n"
				"        A critical section may have been "
				"left unterminated.\n");
	dump_stack();
	ipipe_trace_panic_dump();
}
EXPORT_SYMBOL_GPL(ipipe_root_only);

#endif /* CONFIG_IPIPE_DEBUG_CONTEXT */

#if defined(CONFIG_IPIPE_DEBUG_INTERNAL) && defined(CONFIG_SMP)

int notrace __ipipe_check_percpu_access(void)
{
	struct ipipe_percpu_domain_data *p;
	struct ipipe_domain *this_domain;
	unsigned long flags;
	int ret = 0;

	local_irq_save_hw_notrace(flags);

	this_domain = __raw_get_cpu_var(ipipe_percpu_domain);

	/*
	 * Only the root domain may implement preemptive CPU migration
	 * of tasks, so anything above in the pipeline should be fine.
	 */
	if (this_domain != ipipe_root_domain)
		goto out;

	if (raw_irqs_disabled_flags(flags))
		goto out;

	/*
	 * Last chance: hw interrupts were enabled on entry while
	 * running over the root domain, but the root stage might be
	 * currently stalled, in which case preemption would be
	 * disabled, and no migration could occur.
	 */
	if (this_domain == ipipe_root_domain) {
		p = ipipe_root_cpudom_ptr(); 
		if (test_bit(IPIPE_STALL_FLAG, &p->status))
			goto out;
	}
	/*
	 * Our caller may end up accessing the wrong per-cpu variable
	 * instance due to CPU migration; tell it to complain about
	 * this.
	 */
	ret = 1;
out:
	local_irq_restore_hw_notrace(flags);

	return ret;
}

void __ipipe_spin_unlock_debug(unsigned long flags)
{
	/*
	 * We catch a nasty issue where spin_unlock_irqrestore() on a
	 * regular kernel spinlock is about to re-enable hw interrupts
	 * in a section entered with hw irqs off. This is clearly the
	 * sign of a massive breakage coming. Usual suspect is a
	 * regular spinlock which was overlooked, used within a
	 * section which must run with hw irqs disabled.
	 */
	WARN_ON_ONCE(!raw_irqs_disabled_flags(flags) && irqs_disabled_hw());
}
EXPORT_SYMBOL(__ipipe_spin_unlock_debug);

#endif /* CONFIG_IPIPE_DEBUG_INTERNAL && CONFIG_SMP */

void ipipe_prepare_panic(void)
{
#ifdef CONFIG_PRINTK
	__ipipe_printk_bypass = 1;
#endif
	ipipe_context_check_off();
}
EXPORT_SYMBOL_GPL(ipipe_prepare_panic);

EXPORT_SYMBOL(ipipe_virtualize_irq);
EXPORT_SYMBOL(ipipe_alloc_virq);
EXPORT_SYMBOL(__ipipe_spin_lock_irq);
EXPORT_SYMBOL(__ipipe_spin_unlock_irq);
EXPORT_SYMBOL(__ipipe_spin_lock_irqsave);
EXPORT_SYMBOL(__ipipe_spin_trylock_irq);
EXPORT_SYMBOL(__ipipe_spin_trylock_irqsave);
EXPORT_SYMBOL(__ipipe_spin_unlock_irqrestore);
EXPORT_SYMBOL(ipipe_free_virq);
EXPORT_SYMBOL(ipipe_catch_event);
EXPORT_SYMBOL(ipipe_set_irq_affinity);
EXPORT_SYMBOL(ipipe_send_ipi);
EXPORT_SYMBOL(__ipipe_event_monitors);
#if defined(CONFIG_IPIPE_DEBUG_INTERNAL) && defined(CONFIG_SMP)
EXPORT_SYMBOL(__ipipe_check_percpu_access);
#endif
EXPORT_SYMBOL(ipipe_request_tickdev);
EXPORT_SYMBOL(ipipe_release_tickdev);

EXPORT_SYMBOL(ipipe_critical_enter);
EXPORT_SYMBOL(ipipe_critical_exit);
EXPORT_SYMBOL(ipipe_trigger_irq);
EXPORT_SYMBOL(ipipe_get_sysinfo);
