/* VDSO disabled - only stubs needed for link */
#include <linux/init.h>
#include <asm/vdso.h>

unsigned int vclocks_used __read_mostly;
