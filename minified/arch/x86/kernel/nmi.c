
#include <linux/spinlock.h>
#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <linux/sched/debug.h>
#include <linux/delay.h>
#include <linux/hardirq.h>
#include <linux/ratelimit.h>
/* linux/slab.h removed - no slab functions */
/* linux/export.h removed - no EXPORT_SYMBOL */
#include <linux/atomic.h>
#include <linux/sched/clock.h>

#include <asm/cpu_entry_area.h>
#include <asm/traps.h>
#include <asm/mach_traps.h>
#include <asm/nmi.h>
#include <asm/x86_init.h>
/* reboot.h removed - empty header, nothing used */
#include <asm/cache.h>
#include <asm/nospec-branch.h>
/* asm/sev.h include removed - file is stub, nothing used */

struct nmi_desc {
	raw_spinlock_t lock;
	struct list_head head;
};

static struct nmi_desc nmi_desc[NMI_MAX] = {
	{
		.lock = __RAW_SPIN_LOCK_UNLOCKED(&nmi_desc[0].lock),
		.head = LIST_HEAD_INIT(nmi_desc[0].head),
	},
	{
		.lock = __RAW_SPIN_LOCK_UNLOCKED(&nmi_desc[1].lock),
		.head = LIST_HEAD_INIT(nmi_desc[1].head),
	},
	{
		.lock = __RAW_SPIN_LOCK_UNLOCKED(&nmi_desc[2].lock),
		.head = LIST_HEAD_INIT(nmi_desc[2].head),
	},
	{
		.lock = __RAW_SPIN_LOCK_UNLOCKED(&nmi_desc[3].lock),
		.head = LIST_HEAD_INIT(nmi_desc[3].head),
	},

};

/* nmi_stats struct removed - all fields were write-only, never read */

/* ignore_nmis removed - never written, always 0 */
/* unknown_nmi_panic removed - never set to non-zero */
static DEFINE_RAW_SPINLOCK(nmi_reason_lock);

#define nmi_to_desc(type) (&nmi_desc[type])

/* Stub: nmi_warning_debugfs and nmi_check_duration not needed for minimal kernel */

static int nmi_handle(unsigned int type, struct pt_regs *regs)
{
	struct nmi_desc *desc = nmi_to_desc(type);
	struct nmiaction *a;
	int handled = 0;

	rcu_read_lock();

	/* Simplified: no timing check for minimal kernel */
	list_for_each_entry_rcu(a, &desc->head, list)
		handled += a->handler(type, regs);

	rcu_read_unlock();

	return handled;
}
NOKPROBE_SYMBOL(nmi_handle);

/* __register_nmi_handler removed - never called (~21 LOC) */
/* pci_serr_error, io_check_error, unknown_nmi_error inlined into default_do_nmi */

static DEFINE_PER_CPU(bool, swallow_nmi);
static DEFINE_PER_CPU(unsigned long, last_nmi_rip);

static noinstr void default_do_nmi(struct pt_regs *regs)
{
	unsigned char reason = 0;
	int handled;
	bool b2b = false;

	if (regs->ip == __this_cpu_read(last_nmi_rip))
		b2b = true;
	else
		__this_cpu_write(swallow_nmi, false);

	__this_cpu_write(last_nmi_rip, regs->ip);

	handled = nmi_handle(NMI_LOCAL, regs);
	/* nmi_stats.normal increment removed */
	if (handled) {
		if (handled > 1)
			__this_cpu_write(swallow_nmi, true);
		goto out;
	}

	while (!raw_spin_trylock(&nmi_reason_lock))
		cpu_relax();

	reason = x86_platform.get_nmi_reason();

	if (reason & NMI_REASON_MASK) {
		if (reason & NMI_REASON_SERR) {
			/* pci_serr_error inlined */
			if (!nmi_handle(NMI_SERR, regs)) {
				pr_emerg(
					"NMI: PCI system error (SERR) for reason %02x on CPU %d.\n",
					reason, smp_processor_id());
				pr_emerg(
					"Dazed and confused, but trying to continue\n");
			}
			outb((reason & NMI_REASON_CLEAR_MASK) |
				     NMI_REASON_CLEAR_SERR,
			     NMI_REASON_PORT);
		} else if (reason & NMI_REASON_IOCHK) {
			/* io_check_error inlined */
			unsigned long i;
			if (!nmi_handle(NMI_IO_CHECK, regs)) {
				pr_emerg(
					"NMI: IOCK error (debug interrupt?) for reason %02x on CPU %d.\n",
					reason, smp_processor_id());
				show_regs(regs);
			}
			outb((reason & NMI_REASON_CLEAR_MASK) |
				     NMI_REASON_CLEAR_IOCHK,
			     NMI_REASON_PORT);
			for (i = 0; i < 20000; i++)
				udelay(100);
			outb(reason & ~NMI_REASON_CLEAR_IOCHK, NMI_REASON_PORT);
		}
		/* reassert_nmi inlined */
		{
			int old_reg = -1;
			if (do_i_have_lock_cmos())
				old_reg = current_lock_cmos_reg();
			else
				lock_cmos(0);
			outb(0x8f, 0x70);
			inb(0x71);
			outb(0x0f, 0x70);
			inb(0x71);
			if (old_reg >= 0)
				outb(old_reg, 0x70);
			else
				unlock_cmos();
		}
		raw_spin_unlock(&nmi_reason_lock);
		goto out;
	}
	raw_spin_unlock(&nmi_reason_lock);

	if (b2b && __this_cpu_read(swallow_nmi)) {
	} else {
		/* unknown_nmi_error inlined */
		if (!nmi_handle(NMI_UNKNOWN, regs)) {
			pr_emerg(
				"Uhhuh. NMI received for unknown reason %02x on CPU %d.\n",
				reason, smp_processor_id());
			pr_emerg(
				"Dazed and confused, but trying to continue\n");
		}
	}

out:;
}

enum nmi_states {
	NMI_NOT_RUNNING = 0,
	NMI_EXECUTING,
	NMI_LATCHED,
};
static DEFINE_PER_CPU(enum nmi_states, nmi_state);
static DEFINE_PER_CPU(unsigned long, nmi_cr2);
static DEFINE_PER_CPU(unsigned long, nmi_dr7);

DEFINE_IDTENTRY_RAW(exc_nmi)
{
	irqentry_state_t irq_state;

	/* sev_es_nmi_complete removed - empty stub */

	if (this_cpu_read(nmi_state) != NMI_NOT_RUNNING) {
		this_cpu_write(nmi_state, NMI_LATCHED);
		return;
	}
	this_cpu_write(nmi_state, NMI_EXECUTING);
	this_cpu_write(nmi_cr2, read_cr2());
nmi_restart:

	/* sev_es_ist_enter removed - empty stub */

	this_cpu_write(nmi_dr7, local_db_save());

	irq_state = irqentry_nmi_enter(regs);
	/* inc_irq_stat(__nmi_count) removed - counter never read */
	/* ignore_nmis check removed - variable was always 0 */
	default_do_nmi(regs);

	irqentry_nmi_exit(regs, irq_state);

	local_db_restore(this_cpu_read(nmi_dr7));

	/* sev_es_ist_exit removed - empty stub */

	if (unlikely(this_cpu_read(nmi_cr2) != read_cr2()))
		write_cr2(this_cpu_read(nmi_cr2));
	if (this_cpu_dec_return(nmi_state))
		goto nmi_restart;
}

void local_touch_nmi(void)
{
	__this_cpu_write(last_nmi_rip, 0);
}
