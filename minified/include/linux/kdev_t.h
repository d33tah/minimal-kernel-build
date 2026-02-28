#ifndef _LINUX_KDEV_T_H
#define _LINUX_KDEV_T_H

#define MINORBITS	20
#define MINORMASK	((1U << MINORBITS) - 1)

#define MINOR(dev)	((unsigned int) ((dev) & MINORMASK))
#define MKDEV(ma,mi)	(((ma) << MINORBITS) | (mi))

static __always_inline dev_t old_decode_dev(u16 val)
{
	return MKDEV((val >> 8) & 255, val & 255);
}

#endif
