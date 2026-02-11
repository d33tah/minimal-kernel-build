
#define __GENERATING_BOUNDS_H
#include <linux/page-flags.h>
#include <linux/mmzone.h>
/* Inlined from kbuild.h */
#define DEFINE(sym, val) \
	asm volatile("\n.ascii \"->" #sym " %0 " #val "\"" : : "i"(val))
#define BLANK() asm volatile("\n.ascii \"->\"" : :)
#define OFFSET(sym, str, mem) DEFINE(sym, offsetof(struct str, mem))
#include <linux/log2.h>
#include <linux/spinlock_types.h>

int main(void)
{
	DEFINE(NR_PAGEFLAGS, __NR_PAGEFLAGS);
	DEFINE(MAX_NR_ZONES, __MAX_NR_ZONES);
	DEFINE(SPINLOCK_SIZE, sizeof(spinlock_t));

	return 0;
}
