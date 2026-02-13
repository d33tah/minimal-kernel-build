
#include <linux/spinlock.h>
#ifndef NOKPROBE_SYMBOL
#define NOKPROBE_SYMBOL(fname) /* kprobes disabled */
#endif
#include <linux/sched/debug.h>
#include <linux/delay.h>
#include <linux/hardirq.h>
#include <linux/atomic.h>
#include <linux/sched/clock.h>

#include <asm/cpu_entry_area.h>
#include <asm/traps.h>
#include <asm/io.h>
#define NMI_REASON_PORT 0x61
#define NMI_REASON_SERR 0x80
#define NMI_REASON_IOCHK 0x40
#define NMI_REASON_MASK (NMI_REASON_SERR | NMI_REASON_IOCHK)
#define NMI_REASON_CLEAR_SERR 0x04
#define NMI_REASON_CLEAR_IOCHK 0x08
#define NMI_REASON_CLEAR_MASK 0x0f
static inline unsigned char default_get_nmi_reason(void)
{
	return inb(NMI_REASON_PORT);
}
#include <asm/nmi.h>
#include <asm/x86_init.h>
#include <asm/cache.h>
#include <asm/nospec-branch.h>

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

static DEFINE_RAW_SPINLOCK(nmi_reason_lock);

#define nmi_to_desc(type) (&nmi_desc[type])

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

static DEFINE_PER_CPU(unsigned long, last_nmi_rip);

static noinstr void default_do_nmi(struct pt_regs *regs)
{
	unsigned char reason = 0;

	__this_cpu_write(last_nmi_rip, regs->ip);

	if (nmi_handle(NMI_LOCAL, regs))
		return;

	while (!raw_spin_trylock(&nmi_reason_lock))
		cpu_relax();

	reason = x86_platform.get_nmi_reason();

	if (reason & NMI_REASON_MASK) {
		if (reason & NMI_REASON_SERR) {
			nmi_handle(NMI_SERR, regs);
			outb((reason & NMI_REASON_CLEAR_MASK) |
				     NMI_REASON_CLEAR_SERR,
			     NMI_REASON_PORT);
		} else if (reason & NMI_REASON_IOCHK) {
			nmi_handle(NMI_IO_CHECK, regs);
			outb((reason & NMI_REASON_CLEAR_MASK) |
				     NMI_REASON_CLEAR_IOCHK,
			     NMI_REASON_PORT);
		}
		raw_spin_unlock(&nmi_reason_lock);
		return;
	}
	raw_spin_unlock(&nmi_reason_lock);

	nmi_handle(NMI_UNKNOWN, regs);
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

	if (this_cpu_read(nmi_state) != NMI_NOT_RUNNING) {
		this_cpu_write(nmi_state, NMI_LATCHED);
		return;
	}
	this_cpu_write(nmi_state, NMI_EXECUTING);
	this_cpu_write(nmi_cr2, read_cr2());
nmi_restart:

	this_cpu_write(nmi_dr7, local_db_save());

	irq_state = irqentry_nmi_enter(regs);
	default_do_nmi(regs);

	irqentry_nmi_exit(regs, irq_state);

	local_db_restore(this_cpu_read(nmi_dr7));

	if (unlikely(this_cpu_read(nmi_cr2) != read_cr2()))
		write_cr2(this_cpu_read(nmi_cr2));
	if (this_cpu_dec_return(nmi_state))
		goto nmi_restart;
}

void local_touch_nmi(void)
{
	__this_cpu_write(last_nmi_rip, 0);
}
