#ifndef _LINUX_DAX_H
#define _LINUX_DAX_H
#include <linux/fs.h>
/* S_DAX is 0, so dax_mapping always returns false */
static inline bool dax_mapping(struct address_space *mapping) { return false; }
#endif
