/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KFIFO_H
#define _LINUX_KFIFO_H

/*
 * Minimal kfifo stub - not actually used in minimal kernel
 */

struct __kfifo {
	unsigned int	in;
	unsigned int	out;
	unsigned int	mask;
	unsigned int	esize;
	void		*data;
};

#define __STRUCT_KFIFO_COMMON(datatype, recsize, ptrtype) \
	union { \
		struct __kfifo	kfifo; \
		datatype	*type; \
		const datatype	*const_type; \
		char		(*rectype)[recsize]; \
		ptrtype		*ptr; \
		ptrtype const	*ptr_const; \
	}

#define __STRUCT_KFIFO(type, size, recsize, ptrtype) \
{ \
	__STRUCT_KFIFO_COMMON(type, recsize, ptrtype); \
	type		buf[((size < 2) || (size & (size - 1))) ? -1 : size]; \
}

#define __STRUCT_KFIFO_PTR(type, recsize, ptrtype) \
{ \
	__STRUCT_KFIFO_COMMON(type, recsize, ptrtype); \
	type		buf[0]; \
}

#define DECLARE_KFIFO_PTR(fifo, type)	\
struct __STRUCT_KFIFO_PTR(type, 0, type) fifo

#define DECLARE_KFIFO(fifo, type, size)	\
struct __STRUCT_KFIFO(type, size, 0, type) fifo

/* Stub macros and functions */
#define INIT_KFIFO(fifo) \
	(void)sizeof(&(fifo))

static inline void kfifo_init(void *fifo, void *buffer, unsigned int size)
{
	/* Stub - not used */
}

#endif /* _LINUX_KFIFO_H */
