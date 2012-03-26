#include <linux/ipipe.h>
#include <linux/percpu.h>
#include <linux/irqdesc.h>
#include <linux/cpumask.h>
#include <linux/spinlock.h>
#include <linux/ipipe_tickdev.h>
#include <linux/interrupt.h>

unsigned long __ipipe_mach_hrtimer_freq;

static LIST_HEAD(timers);
static IPIPE_DEFINE_SPINLOCK(lock);

static DEFINE_PER_CPU(struct ipipe_timer *, percpu_timer);

/*
 * Default request method: sets the timer to oneshot mode using host
 * timer method. Do nothing if the host timer does not support one-shot
 * mode.
 */
static void ipipe_timer_default_request(struct ipipe_timer *timer, int steal)
{
	struct clock_event_device *evtdev = timer->host_timer;

	if (!(evtdev->features & CLOCK_EVT_FEAT_ONESHOT))
		return;

	evtdev->set_mode(CLOCK_EVT_MODE_ONESHOT, evtdev);
}

/*
 * Default release method: return the timer to the mode it had when
 * starting.
 */
static void ipipe_timer_default_release(struct ipipe_timer *timer)
{
	struct clock_event_device *evtdev = timer->host_timer;

	evtdev->set_mode(evtdev->mode, evtdev);
	if (evtdev->mode == CLOCK_EVT_MODE_ONESHOT)
		evtdev->set_next_event(timer->freq / HZ, evtdev);
}

void ipipe_host_timer_register(struct clock_event_device *evtdev)
{
	struct ipipe_timer *timer = evtdev->ipipe_timer;

	if (!timer)
		return;

	BUG_ON(!timer->irq);
	if (!timer->request)
		timer->request = ipipe_timer_default_request;
	/* By default, use the same method as linux timer, on ARM at
	   least, most set_next_event methods are safe to be called
	   from xenomai domain anyway */
	if (!timer->set) {
		timer->timer_set = evtdev;
		timer->set = (typeof(timer->set))evtdev->set_next_event;
	}
	BUG_ON(!timer->ack);
	if (!timer->release)
		timer->release = ipipe_timer_default_release;

	if (!timer->name)
		timer->name = evtdev->name;
	if (!timer->rating)
		timer->rating = evtdev->rating;
	timer->freq = (1000000000ULL * evtdev->mult) >> evtdev->shift;
	if (!timer->min_delay_ticks)
		timer->min_delay_ticks =
			(evtdev->min_delta_ns * evtdev->mult) >> evtdev->shift;
	if (!timer->cpumask)
		timer->cpumask = evtdev->cpumask;

	timer->host_timer = evtdev;

	ipipe_timer_register(timer);
}

/*
 * register a timer: maintain them in a list sorted by rating
 */
void ipipe_timer_register(struct ipipe_timer *timer)
{
	struct ipipe_timer *t;
	unsigned long flags;

	if (!timer->timer_set)
		timer->timer_set = timer;
	if (!timer->cpumask)
		timer->cpumask = cpumask_of(smp_processor_id());

	spin_lock_irqsave(&lock, flags);
	list_for_each_entry(t, &timers, holder)
		if (t->rating <= timer->rating) {
			__list_add(&timer->holder, t->holder.prev, &t->holder);
			goto done;
		}
	list_add_tail(&timer->holder, &timers);
  done:
	spin_unlock_irqrestore(&lock, flags);
}

static void ipipe_timer_request_sync(void)
{
	struct ipipe_timer *timer = __ipipe_this_cpu_read(percpu_timer);
	struct clock_event_device *evtdev;
	int steal;

	evtdev = timer->host_timer;

	steal = evtdev != NULL && evtdev->mode != CLOCK_EVT_MODE_UNUSED;

	timer->request(timer, steal);
}

/*
 * choose per-cpu timer: we walk the list, and find the timer with
 * the higher rating
 */
int ipipe_timers_request(void)
{
	struct clock_event_device *evtdev;
	struct ipipe_timer *t;
	unsigned long flags;
	unsigned cpu;

	spin_lock_irqsave(&lock, flags);
	for_each_online_cpu(cpu) {
		list_for_each_entry(t, &timers, holder) {
			if (!cpumask_test_cpu(cpu, t->cpumask))
				continue;

			evtdev = t->host_timer;
			if (!evtdev
			    || evtdev->mode != CLOCK_EVT_MODE_SHUTDOWN)
				goto found;
		}
		printk("I-pipe: could not find timer for cpu #%d\n", cpu);
		goto err_remove_all;

	  found:
		/* Sanity check: check that timers on all cpu have the
		   same frequency, xenomai relies on that. */
		if (!__ipipe_mach_hrtimer_freq)
			__ipipe_mach_hrtimer_freq = t->freq;
		else if (__ipipe_mach_hrtimer_freq != t->freq) {
			printk("I-pipe: timer on cpu #%d has wrong frequency\n",
			       cpu);
			goto err_remove_all;
		}
		per_cpu(ipipe_percpu.hrtimer_irq, cpu) = t->irq;
		per_cpu(percpu_timer, cpu) = t;
	}
	spin_unlock_irqrestore(&lock, flags);

	flags = ipipe_critical_enter(ipipe_timer_request_sync);
	ipipe_timer_request_sync();
	ipipe_critical_exit(flags);

	return 0;

err_remove_all:
	spin_unlock_irqrestore(&lock, flags);

	for_each_online_cpu(cpu) {
		per_cpu(ipipe_percpu.hrtimer_irq, cpu) = 0;
		per_cpu(percpu_timer, cpu) = NULL;
	}
	__ipipe_mach_hrtimer_freq = 0;

	return -ENODEV;
}

static void ipipe_timer_release_sync(void)
{
	struct ipipe_timer *timer = __ipipe_this_cpu_read(percpu_timer);

	timer->release(timer);
}

void ipipe_timers_release(void)
{
	unsigned long flags;
	unsigned cpu;

	flags = ipipe_critical_enter(ipipe_timer_release_sync);
	ipipe_timer_release_sync();
	ipipe_critical_exit(flags);

	for_each_online_cpu(cpu) {
		per_cpu(ipipe_percpu.hrtimer_irq, cpu) = 0;
		per_cpu(percpu_timer, cpu) = NULL;
		__ipipe_mach_hrtimer_freq = 0;
	}
}

static void __ipipe_ack_hrtimer_irq(unsigned int irq, struct irq_desc *desc)
{
	struct ipipe_timer *timer = __ipipe_this_cpu_read(percpu_timer);

	desc->ipipe_ack(irq, desc);
	timer->ack();
	desc->ipipe_end(irq, desc);
}

int ipipe_timer_start(void (*tick_handler)(void),
		      void (*emumode)(enum clock_event_mode mode,
				      struct clock_event_device *cdev),
		      int (*emutick)(unsigned long evt,
				     struct clock_event_device *cdev),
		      unsigned cpu, unsigned long *tmfreq)
{
	struct clock_event_device *evtdev;
	struct ipipe_timer *timer;
	unsigned long flags;
	int steal, rc;

	timer = per_cpu(percpu_timer, cpu);
	evtdev = timer->host_timer;

	flags = ipipe_critical_enter(NULL);

	if (cpu == 0 || timer->irq != per_cpu(ipipe_percpu.hrtimer_irq, 0)) {
		rc = ipipe_request_irq(ipipe_head_domain, timer->irq,
				       (ipipe_irq_handler_t)tick_handler,
				       NULL, __ipipe_ack_hrtimer_irq);
		if (rc < 0)
			goto done;
	}

	steal = evtdev != NULL && evtdev->mode != CLOCK_EVT_MODE_UNUSED;

	if (steal && evtdev->ipipe_stolen == 0) {
		timer->real_mult = evtdev->mult;
		timer->real_shift = evtdev->shift;
		timer->real_set_mode = evtdev->set_mode;
		timer->real_set_next_event = evtdev->set_next_event;
		evtdev->mult = 1;
		evtdev->shift = 0;
		evtdev->set_mode = emumode;
		evtdev->set_next_event = emutick;
		evtdev->ipipe_stolen = 1;
	}
	*tmfreq = timer->freq;

	rc = evtdev ? evtdev->mode : CLOCK_EVT_MODE_UNUSED;

  done:
	ipipe_critical_exit(flags);

	return rc;
}

void ipipe_timer_stop(unsigned cpu)
{
	struct clock_event_device *evtdev;
	struct ipipe_timer *timer;
	unsigned long flags;

	timer = per_cpu(percpu_timer, cpu);
	evtdev = timer->host_timer;

	flags = ipipe_critical_enter(NULL);

	if (evtdev && evtdev->ipipe_stolen) {
		evtdev->mult = timer->real_mult;
		evtdev->shift = timer->real_shift;
		evtdev->set_mode = timer->real_set_mode;
		timer->real_mult = timer->real_shift = 0;
		timer->real_set_mode = NULL;
		timer->real_set_next_event = NULL;
		evtdev->ipipe_stolen = 0;
	}

	ipipe_free_irq(ipipe_head_domain, timer->irq);

	ipipe_critical_exit(flags);
}

void ipipe_timer_set(unsigned long delay)
{
	struct ipipe_timer *timer;

	timer = __ipipe_this_cpu_read(percpu_timer);

	if (delay < timer->min_delay_ticks)
		ipipe_raise_irq(timer->irq);
	else
		timer->set(delay, timer->timer_set);
}

const char *ipipe_timer_name(void)
{
	return per_cpu(percpu_timer, 0)->name;
}

unsigned ipipe_timer_ns2ticks(struct ipipe_timer *timer, unsigned ns)
{
	unsigned long long tmp;
	BUG_ON(!timer->freq);
	tmp = (unsigned long long)ns * timer->freq;
	do_div(tmp, 1000000000);
	return tmp;
}

#if defined(CONFIG_IPIPE_HAVE_HOSTRT) || defined(CONFIG_HAVE_IPIPE_HOSTRT)
/*
 * NOTE: The architecture specific code must only call this function
 * when a clocksource suitable for CLOCK_HOST_REALTIME is enabled.
 * The event receiver is responsible for providing proper locking.
 */
void ipipe_update_hostrt(struct timespec *wall_time, struct timespec *wtm,
			 struct clocksource *clock, u32 mult)
{
	struct ipipe_hostrt_data data;

	ipipe_root_only();
	data.live = 1;
	data.cycle_last = clock->cycle_last;
	data.mask = clock->mask;
	data.mult = mult;
	data.shift = clock->shift;
	data.wall_time_sec = wall_time->tv_sec;
	data.wall_time_nsec = wall_time->tv_nsec;
	data.wall_to_monotonic = *wtm;
	__ipipe_notify_kevent(IPIPE_KEVT_HOSTRT, &data);
}

#endif /* CONFIG_IPIPE_HAVE_HOSTRT */
