 
 

#define __GENERATING_BOUNDS_H
 
#include <linux/page-flags.h>
#include <linux/mmzone.h>
#include <linux/kbuild.h>
#include <linux/log2.h>
#include <linux/spinlock_types.h>

int main(void)
{
	 
	DEFINE(NR_PAGEFLAGS, __NR_PAGEFLAGS);
	DEFINE(MAX_NR_ZONES, __MAX_NR_ZONES);
	DEFINE(SPINLOCK_SIZE, sizeof(spinlock_t));
	 

	return 0;
}
