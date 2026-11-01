// SPDX-License-Identifier: GPL-2.0
#include <linux/irq.h>

#include <asm/i8259.h>

static DEFINE_RAW_SPINLOCK(i8259A_lock);

static unsigned int cached_irq_mask = 0xffff;

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

/*
 * Careful! The 8259A is a fragile beast, it pretty
 * much _has_ to be done exactly like this (mask it
 * first, _then_ send the EOI, and the order of EOI
 * to the two 8259s is important!
 */
static void mask_and_ack_8259A(struct irq_data *data)
{
	unsigned int irq = data->irq;
	unsigned int irqmask = 1 << irq;
	unsigned long flags;

	raw_spin_lock_irqsave(&i8259A_lock, flags);

	cached_irq_mask |= irqmask;

	if (irq & 8) {
		inb(PIC_SLAVE_IMR); /* DUMMY - (do we need this?) */
		outb(cached_slave_mask, PIC_SLAVE_IMR);

		/* 'Specific EOI' to slave */
		outb(0x60 + (irq & 7), PIC_SLAVE_CMD);

		/* 'Specific EOI' to master-IRQ2 */
		outb(0x60 + PIC_CASCADE_IR, PIC_MASTER_CMD);
	} else {
		inb(PIC_MASTER_IMR); /* DUMMY - (do we need this?) */
		outb(cached_master_mask, PIC_MASTER_IMR);
		outb(0x60 + irq, PIC_MASTER_CMD); /* 'Specific EOI to master */
	}
	raw_spin_unlock_irqrestore(&i8259A_lock, flags);
}

static struct irq_chip i8259A_chip = {
	.name = "XT-PIC",
	.irq_unmask = enable_8259A_irq,
	.irq_mask_ack = mask_and_ack_8259A,
};

static void init_8259A(int auto_eoi)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&i8259A_lock, flags);

	outb(0xff, PIC_MASTER_IMR); /* mask all of 8259A-1 */

	/*
	 * outb_pic - this has to work on a wide range of PC hardware.
	 */
	outb_pic(0x11, PIC_MASTER_CMD); /* ICW1: select 8259A-1 init */

	/* ICW2: 8259A-1 IR0-7 mapped to ISA_IRQ_VECTOR(0) */
	outb_pic(ISA_IRQ_VECTOR(0), PIC_MASTER_IMR);

	/* 8259A-1 (the master) has a slave on IR2 */
	outb_pic(1U << PIC_CASCADE_IR, PIC_MASTER_IMR);

	outb_pic(MASTER_ICW4_DEFAULT, PIC_MASTER_IMR);

	outb_pic(0x11, PIC_SLAVE_CMD); /* ICW1: select 8259A-2 init */

	/* ICW2: 8259A-2 IR0-7 mapped to ISA_IRQ_VECTOR(8) */
	outb_pic(ISA_IRQ_VECTOR(8), PIC_SLAVE_IMR);

	/* 8259A-2 is a slave on master's IR2 */
	outb_pic(PIC_CASCADE_IR, PIC_SLAVE_IMR);

	/* (slave's support for AEOI in flat mode is to be investigated) */
	outb_pic(SLAVE_ICW4_DEFAULT, PIC_SLAVE_IMR);

	udelay(100); /* wait for 8259A to initialize */

	outb(cached_master_mask, PIC_MASTER_IMR); /* restore master IRQ mask */
	outb(cached_slave_mask, PIC_SLAVE_IMR); /* restore slave IRQ mask */

	raw_spin_unlock_irqrestore(&i8259A_lock, flags);
}

static void legacy_pic_int_noop(int unused) {};

struct legacy_pic null_legacy_pic = {
	.nr_legacy_irqs = 0,
	.chip = &dummy_irq_chip,
	.init = legacy_pic_int_noop,
};

static struct legacy_pic default_legacy_pic = {
	.nr_legacy_irqs = NR_IRQS_LEGACY,
	.chip = &i8259A_chip,
	.init = init_8259A,
};

struct legacy_pic *legacy_pic = &default_legacy_pic;
