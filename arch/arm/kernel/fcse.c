#include <linux/bitops.h>
#include <linux/memory.h>
#include <linux/spinlock.h>
#include <linux/mm.h>

#include <asm/fcse.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#define NR_PIDS (TASK_SIZE / FCSE_PID_TASK_SIZE)
#define PIDS_LONGS ((NR_PIDS + BITS_PER_LONG - 1) / BITS_PER_LONG)

static IPIPE_DEFINE_SPINLOCK(fcse_lock);
static unsigned long fcse_pids_bits[PIDS_LONGS];
unsigned long fcse_pids_cache_dirty[PIDS_LONGS];

#ifdef CONFIG_ARM_FCSE_BEST_EFFORT
static unsigned random_pid;
struct fcse_user fcse_pids_user[NR_PIDS];
#endif /* CONFIG_ARM_FCSE_BEST_EFFORT */

static void fcse_pid_reference_inner(unsigned pid)
{
#ifdef CONFIG_ARM_FCSE_BEST_EFFORT
	if (++fcse_pids_user[pid].count == 1)
#endif /* CONFIG_ARM_FCSE_BEST_EFFORT */
		__set_bit(pid, fcse_pids_bits);
}

static void fcse_pid_dereference(struct mm_struct *mm)
{
#ifdef CONFIG_ARM_FCSE_BEST_EFFORT
	unsigned pid = mm->context.fcse.pid >> FCSE_PID_SHIFT;

	if (--fcse_pids_user[pid].count == 0)
		__clear_bit(pid, fcse_pids_bits);

	/*
	 * The following means we suppose that by the time this
	 * function is called, this mm is out of cache:
	 * - when the caller is destroy_context, exit_mmap is called
	 * by mmput before, which flushes the cache;
	 * - when the caller is fcse_relocate_mm_to_null_pid, we flush
	 * the cache in this function.
	 */
	if (fcse_pids_user[pid].mm == mm) {
		fcse_pids_user[pid].mm = NULL;
		__clear_bit(pid, fcse_pids_cache_dirty);
	}
#else /* CONFIG_ARM_FCSE_BEST_EFFORT */
	__clear_bit(pid, fcse_pids_bits);
	__clear_bit(pid, fcse_pids_cache_dirty);
#endif /* CONFIG_ARM_FCSE_BEST_EFFORT */
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
#ifdef CONFIG_ARM_FCSE_BEST_EFFORT
			if(++random_pid == NR_PIDS)
				random_pid = 0;
			pid = random_pid;
#else /* CONFIG_ARM_FCSE_GUARANTEED */
			spin_unlock_irqrestore(&fcse_lock, flags);
			return -EAGAIN;
#endif /* CONFIG_ARM_FCSE_GUARANTEED */
		}
	}
	fcse_pid_reference_inner(pid);
	spin_unlock_irqrestore(&fcse_lock, flags);

	return pid;
}

void fcse_pid_free(struct mm_struct *mm)
{
	unsigned long flags;

	spin_lock_irqsave(&fcse_lock, flags);
	fcse_pid_dereference(mm);
	spin_unlock_irqrestore(&fcse_lock, flags);
}

static void fcse_notify_flush_all_inner(struct mm_struct *next)
{
	unsigned long flags;

	spin_lock_irqsave(&fcse_lock, flags);
	switch(ARRAY_SIZE(fcse_pids_cache_dirty)) {
	case 4:
		fcse_pids_cache_dirty[3] = 0UL;
	case 3:
		fcse_pids_cache_dirty[2] = 0UL;
	case 2:
		fcse_pids_cache_dirty[1] = 0UL;
	case 1:
		fcse_pids_cache_dirty[0] = 0UL;
	}
	if (next != &init_mm && next) {
		unsigned fcse_pid = next->context.fcse.pid >> FCSE_PID_SHIFT;
		__set_bit(fcse_pid, fcse_pids_cache_dirty);
	}
	spin_unlock_irqrestore(&fcse_lock, flags);
}

void fcse_notify_flush_all(void)
{
	fcse_notify_flush_all_inner(current->mm);
}

#ifdef CONFIG_ARM_FCSE_BEST_EFFORT
int fcse_needs_flush(struct mm_struct *prev, struct mm_struct *next)
{
	unsigned fcse_pid = next->context.fcse.pid >> FCSE_PID_SHIFT;
	unsigned res, reused_pid = 0;
	unsigned long flags;

	spin_lock_irqsave(&fcse_lock, flags);
	if (fcse_pids_user[fcse_pid].mm != next) {
		if (fcse_pids_user[fcse_pid].mm)
			reused_pid = test_bit(fcse_pid, fcse_pids_cache_dirty);
		fcse_pids_user[fcse_pid].mm = next;
	}
	__set_bit(fcse_pid, fcse_pids_cache_dirty);
	spin_unlock_irqrestore(&fcse_lock, flags);

	res = reused_pid
		|| next->context.fcse.high_pages
		|| !prev
		|| prev->context.fcse.shared_dirty_pages
		|| prev->context.fcse.high_pages;

	if (res) {
		cpu_clear(smp_processor_id(), prev->cpu_vm_mask);
		fcse_notify_flush_all_inner(next);
	}

	return res;
}
EXPORT_SYMBOL_GPL(fcse_needs_flush);

void fcse_pid_reference(unsigned fcse_pid)
{
	unsigned long flags;

	spin_lock_irqsave(&fcse_lock, flags);
	fcse_pid_reference_inner(fcse_pid);
	spin_unlock_irqrestore(&fcse_lock, flags);
}

/* Called with mm->mmap_sem write-locked. */
void fcse_relocate_mm_to_null_pid(struct mm_struct *mm)
{
	pgd_t *to = mm->pgd + pgd_index(0);
	pgd_t *from = pgd_offset(mm, 0);
	unsigned len = pgd_index(FCSE_TASK_SIZE) * sizeof(*from);
	unsigned long flags;

	preempt_disable();

	memcpy(to, from, len);
	spin_lock_irqsave(&fcse_lock, flags);
	fcse_pid_dereference(mm);
	fcse_pid_reference_inner(0);
	fcse_pids_user[0].mm = mm;
	spin_unlock_irqrestore(&fcse_lock, flags);

	mm->context.fcse.pid = 0;
	fcse_pid_set(0);
	memset(from, '\0', len);
	mb();
	flush_cache_mm(mm);
	flush_tlb_mm(mm);

	preempt_enable();
}
#endif /* CONFIG_ARM_FCSE_BEST_EFFORT */
