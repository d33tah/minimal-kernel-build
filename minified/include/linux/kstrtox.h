#ifndef _LINUX_KSTRTOX_H
#define _LINUX_KSTRTOX_H

#include <linux/compiler.h>
#include <linux/types.h>

extern unsigned long simple_strtoul(const char *,char **,unsigned int);
extern unsigned long long simple_strtoull(const char *,char **,unsigned int);

#endif
