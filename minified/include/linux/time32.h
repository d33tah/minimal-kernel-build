#ifndef _LINUX_TIME32_H
#define _LINUX_TIME32_H
#include <linux/time64.h>
typedef s32 old_time32_t;
struct old_timespec32 { old_time32_t tv_sec; s32 tv_nsec; };
#endif
