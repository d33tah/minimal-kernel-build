#ifndef _LINUX_TIME_H
#define _LINUX_TIME_H

# include <linux/cache.h>
# include <linux/math64.h>
# include <linux/time64.h>

# include <linux/time32.h>


/* Inlined from vdso/time.h */
struct timens_offset { s64	sec; u64	nsec; };

#endif
