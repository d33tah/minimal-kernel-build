 
#ifndef _UAPI_ASM_X86_SIGCONTEXT_H
#define _UAPI_ASM_X86_SIGCONTEXT_H

 

#include <linux/compiler.h>
#include <linux/types.h>

/* FP_XSTATE_MAGIC1 removed - never used */
#define FP_XSTATE_MAGIC2		0x46505845U
#define FP_XSTATE_MAGIC2_SIZE		sizeof(FP_XSTATE_MAGIC2)

 
struct _fpx_sw_bytes {
	 
	__u32				magic1;

	 
	__u32				extended_size;

	 
	__u64				xfeatures;

	 
	__u32				xstate_size;

	 
	__u32				padding[7];
};

 

 
struct _fpreg {
	__u16				significand[4];
	__u16				exponent;
};

 
struct _fpxreg {
	__u16				significand[4];
	__u16				exponent;
	__u16				padding[3];
};

 
struct _xmmreg {
	__u32				element[4];
};

/* X86_FXSR_MAGIC removed - never used */

 
struct _fpstate_32 {
	 
	__u32				cw;
	__u32				sw;
	__u32				tag;
	__u32				ipoff;
	__u32				cssel;
	__u32				dataoff;
	__u32				datasel;
	struct _fpreg			_st[8];
	__u16				status;
	__u16				magic;		 
							 

	 
	__u32				_fxsr_env[6];	 
	__u32				mxcsr;
	__u32				reserved;
	struct _fpxreg			_fxsr_st[8];	 
	struct _xmmreg			_xmm[8];	 
	union {
		__u32			padding1[44];	 
		__u32			padding[44];	 
	};

	union {
		__u32			padding2[12];
		struct _fpx_sw_bytes	sw_reserved;	 
	};
};

/* 32-bit only kernel - _fpstate_64 removed */
#define _fpstate _fpstate_32

struct _header {
	__u64				xfeatures;
	__u64				reserved1[2];
	__u64				reserved2[5];
};

/* struct _ymmh_state and struct _xstate removed - never used */

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
#ifdef __KERNEL__
#define sigcontext sigcontext_32
#endif

/* Userspace-only sigcontext struct removed - __KERNEL__ always defined */

#endif  
