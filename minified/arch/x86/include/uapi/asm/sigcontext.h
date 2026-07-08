 
#ifndef _UAPI_ASM_X86_SIGCONTEXT_H
#define _UAPI_ASM_X86_SIGCONTEXT_H

 

#include <linux/compiler.h>
#include <linux/types.h>

/* FP_XSTATE_MAGIC1/2/2_SIZE removed - 0-ref (MAGIC2 only fed MAGIC2_SIZE,
 * which had no consumer; userspace fpstate signal frame not built kernel-side). */

/* struct _fpx_sw_bytes / _fpreg / _fpxreg / _xmmreg / _fpstate_32 removed -
 * the whole _fpstate_32 cluster + its _fpstate/_fpstate_ia32 aliases +
 * X86_FXSR_MAGIC were 0-ref in the kernel (only consumed by the userspace
 * #ifndef __KERNEL__ sigcontext block, which is never compiled here). */



struct sigcontext_32 {
	__u16				gs, __gsh;
	__u16				fs, __fsh;
	__u16				es, __esh;
	__u16				ds, __dsh;
	__u32				di;
	__u32				si;
	__u32				bp;
	__u32				sp;
	__u32				bx;
	__u32				dx;
	__u32				cx;
	__u32				ax;
	__u32				trapno;
	__u32				err;
	__u32				ip;
	__u16				cs, __csh;
	__u32				flags;
	__u32				sp_at_signal;
	__u16				ss, __ssh;

	 
	__u32				fpstate;  
	__u32				oldmask;
	__u32				cr2;
};

/* 32-bit only kernel - sigcontext_64 removed */
#define sigcontext sigcontext_32

/* userspace-only #ifndef __KERNEL__ sigcontext + _fpstate/_fpstate_ia32
 * aliases removed - never compiled in this kernel-only build, 0-ref. */

#endif
