#include <linux/bitops.h>
#include <linux/memory.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <asm/fcse.h>

#define MAX_PID (MODULE_START / FCSE_PID_TASK_SIZE)
#define PIDS_LONGS ((MAX_PID + 8 * sizeof(long) - 1) / (8 * sizeof(long)))

static DEFINE_SPINLOCK(fcse_lock);
static unsigned long fcse_pids_bits[PIDS_LONGS];

int fcse_pid_alloc(void)
{
	unsigned long flags;
	unsigned bit;

	spin_lock_irqsave(&fcse_lock, flags);
	bit = find_first_zero_bit(fcse_pids_bits, MAX_PID);
	if (bit == MAX_PID) {
		spin_unlock(&fcse_lock);
		return -1;
	}
	set_bit(bit, fcse_pids_bits);
	spin_unlock_irqrestore(&fcse_lock, flags);

	return bit;
}

void fcse_pid_free(unsigned pid)
{
	unsigned long flags;

	spin_lock_irqsave(&fcse_lock, flags);
	pid = test_and_clear_bit(pid, fcse_pids_bits);
	spin_unlock_irqrestore(&fcse_lock, flags);
}
