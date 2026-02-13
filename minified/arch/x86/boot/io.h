
#ifndef BOOT_IO_H
#define BOOT_IO_H

/* shared/io.h inlined */
#include <linux/types.h>
#define BUILDIO(bwl, bw, type)						\
static inline void __out##bwl(type value, u16 port)			\
{									\
	asm volatile("out" #bwl " %" #bw "0, %w1"			\
		     : : "a"(value), "Nd"(port));			\
}									\
static inline type __in##bwl(u16 port)					\
{									\
	type value;							\
	asm volatile("in" #bwl " %w1, %" #bw "0"			\
		     : "=a"(value) : "Nd"(port));			\
	return value;							\
}
BUILDIO(b, b, u8)
BUILDIO(w, w, u16)
BUILDIO(l,  , u32)
#undef BUILDIO

#undef inb
#undef inw
#undef inl
#undef outb
#undef outw
#undef outl

struct port_io_ops {
	u8	(*f_inb)(u16 port);
	void	(*f_outb)(u8 v, u16 port);
	void	(*f_outw)(u16 v, u16 port);
};

extern struct port_io_ops pio_ops;


static inline void init_default_io_ops(void)
{
	pio_ops.f_inb  = __inb;
	pio_ops.f_outb = __outb;
	pio_ops.f_outw = __outw;
}


#define inb  pio_ops.f_inb
#define outb pio_ops.f_outb
#define outw pio_ops.f_outw

#endif
