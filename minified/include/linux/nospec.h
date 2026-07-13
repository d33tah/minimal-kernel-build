
#ifndef _LINUX_NOSPEC_H
#define _LINUX_NOSPEC_H

#include <linux/compiler.h>
#include <asm/barrier.h>

#define array_index_nospec(index, size)					({										typeof(index) _i = (index);						typeof(size) _s = (size);						unsigned long _mask = array_index_mask_nospec(_i, _s);												BUILD_BUG_ON(sizeof(_i) > sizeof(long));				BUILD_BUG_ON(sizeof(_s) > sizeof(long));													(typeof(_i)) (_i & _mask);					})

#endif
