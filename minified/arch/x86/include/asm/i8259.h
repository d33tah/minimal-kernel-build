 
#ifndef _ASM_X86_I8259_H
#define _ASM_X86_I8259_H

#include <linux/delay.h>
#include <asm/io.h>

extern unsigned int cached_irq_mask;

#define __byte(x, y)		(((unsigned char *)&(y))[x])
#define cached_master_mask	(__byte(0, cached_irq_mask))
#define cached_slave_mask	(__byte(1, cached_irq_mask))

 
#define PIC_MASTER_CMD		0x20
#define PIC_MASTER_IMR		0x21
#define PIC_MASTER_ISR		PIC_MASTER_CMD
#define PIC_SLAVE_CMD		0xa0
#define PIC_SLAVE_IMR		0xa1

 
#define PIC_CASCADE_IR		2
#define MASTER_ICW4_DEFAULT	0x01
#define SLAVE_ICW4_DEFAULT	0x01
#define PIC_ICW4_AEOI		2

extern raw_spinlock_t i8259A_lock;

 

static inline void outb_pic(unsigned char value, unsigned int port)
{
	outb(value, port);
	 
	udelay(2);
}

extern struct irq_chip i8259A_chip;

struct legacy_pic {
	int nr_legacy_irqs;
	struct irq_chip *chip;
	void (*init)(int auto_eoi);
	/* mask, unmask, mask_all, restore_mask, probe, irq_pending, make_irq removed - unused */
};

extern struct legacy_pic *legacy_pic;
extern struct legacy_pic null_legacy_pic;

static inline bool has_legacy_pic(void)
{
	return legacy_pic != &null_legacy_pic;
}

static inline int nr_legacy_irqs(void)
{
	return legacy_pic->nr_legacy_irqs;
}

#endif  
