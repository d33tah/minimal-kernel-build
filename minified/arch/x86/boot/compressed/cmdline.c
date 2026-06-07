#include "misc.h"

static unsigned long fs;
static inline void set_fs(unsigned long seg)
{
	fs = seg << 4;
}
typedef unsigned long addr_t;
static inline char rdfs8(addr_t addr)
{
	return *((char *)(fs + addr));
}
#include "../cmdline.c"
