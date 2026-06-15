#ifndef _LINUX_KSTRTOX_H
#define _LINUX_KSTRTOX_H

#include <linux/compiler.h>
#include <linux/types.h>

int __must_check kstrtoull(const char *s, unsigned int base, unsigned long long *res);

int __must_check kstrtouint(const char *s, unsigned int base, unsigned int *res);


extern unsigned long simple_strtoul(const char *,char **,unsigned int);
extern unsigned long long simple_strtoull(const char *,char **,unsigned int);

#endif
