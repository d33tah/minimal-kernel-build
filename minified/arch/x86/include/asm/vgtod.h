 
#ifndef _ASM_X86_VGTOD_H
#define _ASM_X86_VGTOD_H

 
#include <linux/compiler.h>
/* clocksource.h inlined */
extern unsigned int vclocks_used;
static inline void vclocks_set_used(unsigned int which) { WRITE_ONCE(vclocks_used, READ_ONCE(vclocks_used) | (1 << which)); }
#include <vdso/datapage.h>
#include <vdso/helpers.h>

#include <uapi/linux/time.h>

/* gtod_long_t removed - unused */

#endif  
