#include <linux/bitops.h>
#include <linux/memory.h>
#include <linux/spinlock.h>
#include <linux/mm.h>

#include <asm/fcse.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#define NR_PIDS (TASK_SIZE / FCSE_PID_TASK_SIZE)
#define PIDS_LONGS ((NR_PIDS + BITS_PER_LONG - 1) / BITS_PER_LONG)

static DEFINE_SPINLOCK(fcse_lock);
static unsigned long fcse_pids_bits[PIDS_LONGS];
	
static void fcse_pid_reference_inner(unsigned pid)
{
	__set_bit(pid, fcse_pids_bits);
}

static void fcse_pid_dereference(unsigned pid)
{
	__clear_bit(pid, fcse_pids_bits);
}

int fcse_pid_alloc(void)
{
	unsigned long flags;
	unsigned pid;

	spin_lock_irqsave(&fcse_lock, flags);
	pid = find_next_zero_bit(fcse_pids_bits, NR_PIDS, 1);
	if (pid == NR_PIDS) {
		/* Allocate zero pid last, since zero pid is also used by
		   processes with address space larger than 32MB in
		   best-effort mode. */
		if (!test_bit(0, fcse_pids_bits))
			pid = 0;
		else {
			spin_unlock_irqrestore(&fcse_lock, flags);
			return -EAGAIN;
		}
	}
	fcse_pid_reference_inner(pid);
	spin_unlock_irqrestore(&fcse_lock, flags);

	return pid;
}

void fcse_pid_free(unsigned pid)
{
	unsigned long flags;

	spin_lock_irqsave(&fcse_lock, flags);
	fcse_pid_dereference(pid);
	spin_unlock_irqrestore(&fcse_lock, flags);
}
