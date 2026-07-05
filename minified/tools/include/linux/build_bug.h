
#ifndef _LINUX_BUILD_BUG_H
#define _LINUX_BUILD_BUG_H

#include <linux/compiler.h>

#define BUILD_BUG_ON_ZERO(e) ((int)(sizeof(struct { int:(-!!(e)); })))

#endif
