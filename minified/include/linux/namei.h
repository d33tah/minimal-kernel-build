#ifndef _LINUX_NAMEI_H
#define _LINUX_NAMEI_H
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/path.h>
#include <linux/fcntl.h>
#include <linux/errno.h>
enum { MAX_NESTED_LINKS = 8 };
#define MAXSYMLINKS 40
enum {LAST_NORM, LAST_ROOT, LAST_DOT, LAST_DOTDOT};
#define LOOKUP_FOLLOW		0x0001
#define LOOKUP_DIRECTORY	0x0002
#define LOOKUP_EMPTY		0x4000
#define LOOKUP_DOWN		0x8000
#define LOOKUP_MOUNTPOINT	0x0080
#define LOOKUP_REVAL		0x0020
#define LOOKUP_RCU		0x0040
#define LOOKUP_OPEN		0x0100
#define LOOKUP_CREATE		0x0200
#define LOOKUP_EXCL		0x0400
#define LOOKUP_PARENT		0x0010
#define LOOKUP_NO_SYMLINKS	0x010000
#define LOOKUP_NO_XDEV		0x040000
#define LOOKUP_BENEATH		0x080000
#define LOOKUP_IN_ROOT		0x100000
#define LOOKUP_CACHED		0x200000
#define LOOKUP_IS_SCOPED (LOOKUP_BENEATH | LOOKUP_IN_ROOT)
extern int kern_path(const char *, unsigned, struct path *);
extern struct dentry *kern_path_create(int, const char *, struct path *, unsigned int);
extern void done_path_create(struct path *, struct dentry *);
static inline void nd_terminate_link(void *name, size_t len, size_t maxlen) { ((char *) name)[min(len, maxlen)] = '\0'; }
#endif
