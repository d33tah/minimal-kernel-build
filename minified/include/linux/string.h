#ifndef _LINUX_STRING_H_
#define _LINUX_STRING_H_

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/stddef.h>
/* linux/errno.h removed - no errno constants used */
#include <linux/stdarg.h>

/* memdup_user removed - never called */
#include <asm/string_32.h>

/* strcpy, strncpy, strcmp, strncmp, strchr, strlen, strnlen, strstr,
   memset, memcpy, memmove declarations removed - provided by
   asm/string_32.h via __HAVE_ARCH_* defines */

#ifndef __HAVE_ARCH_STRLCPY
size_t strlcpy(char *, const char *, size_t);
#endif
#ifndef __HAVE_ARCH_STRSCPY
ssize_t strscpy(char *, const char *, size_t);
#endif

ssize_t strscpy_pad(char *dest, const char *src, size_t count);

#ifndef __HAVE_ARCH_STRLCAT
extern size_t strlcat(char *, const char *, __kernel_size_t);
#endif
#ifndef __HAVE_ARCH_STRNCHR
extern char * strnchr(const char *, size_t, int);
#endif
#ifndef __HAVE_ARCH_STRRCHR
extern char * strrchr(const char *,int);
#endif
extern char * __must_check skip_spaces(const char *);

#ifndef __HAVE_ARCH_STRPBRK
extern char * strpbrk(const char *,const char *);
#endif
#ifndef __HAVE_ARCH_STRSEP
extern char * strsep(char **,const char *);
#endif
/* strcspn removed - never called or implemented */

#ifndef __HAVE_ARCH_MEMCMP
extern int memcmp(const void *,const void *,__kernel_size_t);
#endif
/* memchr removed - never called */

/* strreplace removed - never called */

extern void kfree_const(const void *x);

/* kstrdup removed - only used in mm/util.c */
extern const char *kstrdup_const(const char *s, gfp_t gfp);
extern void *kmemdup(const void *src, size_t len, gfp_t gfp);
extern char *kmemdup_nul(const char *s, size_t len, gfp_t gfp);


/* memzero_explicit, kbasename inlined at single call sites */

#endif
