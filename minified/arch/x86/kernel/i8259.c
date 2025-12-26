#include <linux/linkage.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/timex.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/bitops.h>
#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pgtable.h>

#include <linux/atomic.h>
#include <asm/timer.h>
#include <asm/hw_irq.h>
#include <asm/desc.h>
#include <asm/apic.h>
#include <asm/i8259.h>

static void init_8259A(int auto_eoi);

static int i8259A_auto_eoi;
DEFINE_RAW_SPINLOCK(i8259A_lock);

unsigned int cached_irq_mask = 0xffff;

unsigned long io_apic_irqs;

static void disable_8259A_irq(struct irq_data *data)
{
	unsigned int irq = data->irq;
	unsigned int mask = 1 << irq;
	unsigned long flags;

	raw_spin_lock_irqsave(&i8259A_lock, flags);
	cached_irq_mask |= mask;
	if (irq & 8)
		outb(cached_slave_mask, PIC_SLAVE_IMR);
	else
		outb(cached_master_mask, PIC_MASTER_IMR);
	raw_spin_unlock_irqrestore(&i8259A_lock, flags);
}

static void enable_8259A_irq(struct irq_data *data)
{
	unsigned int irq = data->irq;
	unsigned int mask = ~(1 << irq);
	unsigned long flags;

	raw_spin_lock_irqsave(&i8259A_lock, flags);
	cached_irq_mask &= mask;
	if (irq & 8)
		outb(cached_slave_mask, PIC_SLAVE_IMR);
	else
		outb(cached_master_mask, PIC_MASTER_IMR);
	raw_spin_unlock_irqrestore(&i8259A_lock, flags);
}

static inline int i8259A_irq_real(unsigned int irq)
{
	int value;
	int irqmask = 1 << irq;

	if (irq < 8) {
		outb(0x0B, PIC_MASTER_CMD);
		value = inb(PIC_MASTER_CMD) & irqmask;
		outb(0x0A, PIC_MASTER_CMD);
		return value;
	}
	outb(0x0B, PIC_SLAVE_CMD);
	value = inb(PIC_SLAVE_CMD) & (irqmask >> 8);
	outb(0x0A, PIC_SLAVE_CMD);
	return value;
}

static void mask_and_ack_8259A(struct irq_data *data)
{
	unsigned int irq = data->irq;
	unsigned int irqmask = 1 << irq;
	unsigned long flags;

	raw_spin_lock_irqsave(&i8259A_lock, flags);

	if (cached_irq_mask & irqmask)
		goto spurious_8259A_irq;
	cached_irq_mask |= irqmask;

handle_real_irq:
	if (irq & 8) {
		inb(PIC_SLAVE_IMR);
		outb(cached_slave_mask, PIC_SLAVE_IMR);

		outb(0x60 + (irq & 7), PIC_SLAVE_CMD);

		outb(0x60 + PIC_CASCADE_IR, PIC_MASTER_CMD);
	} else {
		inb(PIC_MASTER_IMR);
		outb(cached_master_mask, PIC_MASTER_IMR);
		outb(0x60 + irq, PIC_MASTER_CMD);
	}
	raw_spin_unlock_irqrestore(&i8259A_lock, flags);
	return;

spurious_8259A_irq:

	if (i8259A_irq_real(irq))

		goto handle_real_irq;

	{
		static int spurious_irq_mask;

		if (!(spurious_irq_mask & irqmask)) {
			printk_deferred(KERN_DEBUG
					"spurious 8259A interrupt: IRQ%d.\n",
					irq);
			spurious_irq_mask |= irqmask;
		}
		atomic_inc(&irq_err_count);

		goto handle_real_irq;
	}
}

struct irq_chip i8259A_chip = {
	.name = "XT-PIC",
	.irq_mask = disable_8259A_irq,
	.irq_disable = disable_8259A_irq,
	.irq_unmask = enable_8259A_irq,
	.irq_mask_ack = mask_and_ack_8259A,
};

static void init_8259A(int auto_eoi)
{
	unsigned long flags;

	i8259A_auto_eoi = auto_eoi;

	raw_spin_lock_irqsave(&i8259A_lock, flags);

	outb(0xff, PIC_MASTER_IMR);

	outb_pic(0x11, PIC_MASTER_CMD);

	outb_pic(ISA_IRQ_VECTOR(0), PIC_MASTER_IMR);

	outb_pic(1U << PIC_CASCADE_IR, PIC_MASTER_IMR);

	if (auto_eoi)
		outb_pic(MASTER_ICW4_DEFAULT | PIC_ICW4_AEOI, PIC_MASTER_IMR);
	else
		outb_pic(MASTER_ICW4_DEFAULT, PIC_MASTER_IMR);

	outb_pic(0x11, PIC_SLAVE_CMD);

	outb_pic(ISA_IRQ_VECTOR(8), PIC_SLAVE_IMR);

	outb_pic(PIC_CASCADE_IR, PIC_SLAVE_IMR);

	outb_pic(SLAVE_ICW4_DEFAULT, PIC_SLAVE_IMR);

	if (auto_eoi)

		i8259A_chip.irq_mask_ack = disable_8259A_irq;
	else
		i8259A_chip.irq_mask_ack = mask_and_ack_8259A;

	udelay(100);

	outb(cached_master_mask, PIC_MASTER_IMR);
	outb(cached_slave_mask, PIC_SLAVE_IMR);

	raw_spin_unlock_irqrestore(&i8259A_lock, flags);
}

static void legacy_pic_int_noop(int unused) {};

struct legacy_pic null_legacy_pic = {
	.nr_legacy_irqs = 0,
	.chip = &dummy_irq_chip,
	.init = legacy_pic_int_noop,
};

struct legacy_pic default_legacy_pic = {
	.nr_legacy_irqs = NR_IRQS_LEGACY,
	.chip = &i8259A_chip,
	.init = init_8259A,
};

struct legacy_pic *legacy_pic = &default_legacy_pic;
