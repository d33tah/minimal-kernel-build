#ifndef _LINUX_KSTRTOX_H
#define _LINUX_KSTRTOX_H

#include <linux/compiler_attributes.h>
#include <linux/types.h>

int __must_check kstrtoull(const char *s, unsigned int base, unsigned long long *res);
int __must_check kstrtouint(const char *s, unsigned int base, unsigned int *res);
/* kstrtoll, kstrtoul, kstrtol, kstrtoint, kstrtou16, kstrtos16, kstrtou8,
   kstrtobool, _kstrtoul, _kstrtol removed - no callers */

extern unsigned long simple_strtoul(const char *,char **,unsigned int);
/* simple_strtoull made static in lib/vsprintf.c */

#endif
