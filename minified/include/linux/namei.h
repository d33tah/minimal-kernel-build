#ifndef _LINUX_NAMEI_H
#define _LINUX_NAMEI_H
#include <linux/fs.h>
/* linux/kernel.h removed - no kernel.h macros used */
#include <linux/fcntl.h>
/* linux/errno.h removed - no errno constants used */
/* MAX_NESTED_LINKS, MAXSYMLINKS removed - symlinks not supported */
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
/* LOOKUP_NO_SYMLINKS, LOOKUP_NO_XDEV, LOOKUP_BENEATH, LOOKUP_IN_ROOT, LOOKUP_CACHED, LOOKUP_IS_SCOPED removed - never set */
extern int kern_path(const char *, unsigned, struct path *);
/* kern_path_create and done_path_create removed - never called */
/* nd_terminate_link removed - only used by page_get_link which was removed */
#endif
