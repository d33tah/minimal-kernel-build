#ifndef _LINUX_NAMEI_H
#define _LINUX_NAMEI_H
#include <linux/fs.h>
enum {LAST_NORM, LAST_ROOT, LAST_DOT, LAST_DOTDOT};
#define LOOKUP_FOLLOW		0x0001
#define LOOKUP_DIRECTORY	0x0002
#define LOOKUP_RCU		0x0040
#define LOOKUP_OPEN		0x0100
#define LOOKUP_CREATE		0x0200
#define LOOKUP_EXCL		0x0400
#define LOOKUP_PARENT		0x0010
extern int kern_path(const char *, unsigned, struct path *);
#endif
