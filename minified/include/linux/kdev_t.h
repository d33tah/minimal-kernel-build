#ifndef _LINUX_KDEV_T_H
#define _LINUX_KDEV_T_H



#define MINORBITS	20
#define MINORMASK	((1U << MINORBITS) - 1)

#define MAJOR(dev)	((unsigned int) ((dev) >> MINORBITS))
#define MINOR(dev)	((unsigned int) ((dev) & MINORMASK))
#define MKDEV(ma,mi)	(((ma) << MINORBITS) | (mi))

/* print_dev_t removed - no callers */

static __always_inline dev_t old_decode_dev(u16 val)
{
	return MKDEV((val >> 8) & 255, val & 255);
}

/* new_decode_dev removed - no callers */

#endif
