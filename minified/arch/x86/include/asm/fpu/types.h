 
 
#ifndef _ASM_X86_FPU_H
#define _ASM_X86_FPU_H

 
struct fregs_state {
	u32 cwd, swd, twd, fip, fcs, foo, fos;

	 
	u32			st_space[20];

	 
	u32			status;
};

 
struct fxregs_state {
	u16 cwd, swd, twd, fop;
	union {
		struct {
			u64 rip, rdp;
		};
		struct {
			u32 fip, fcs, foo, fos;
		};
	};
	u32 mxcsr, mxcsr_mask;

	 
	u32			st_space[32];

	 
	u32			xmm_space[64];

	u32			padding[12];

	union {
		u32 padding1[12], sw_reserved[12];
	};

} __attribute__((aligned(16)));

 
#define MXCSR_DEFAULT		0x1f80

 

 
struct swregs_state {
	u32 cwd, swd, twd, fip, fcs, foo, fos;
	 
	u32			st_space[20];
	u8 ftop, changed, lookahead, no_update, rm, alimit;
	struct math_emu_info	*info;
	u32			entry_eip;
};

/*
 * enum xfeature and the XFEATURE_MASK_* per-component masks removed: their
 * only consumers were the composite masks in asm/fpu/xstate.h, themselves
 * removed once restore_fpregs_from_fpstate() stopped taking a restore mask
 * (no XSAVE on this build). The hardware xstate structs below are kept.
 */

struct xstate_header {
	u64 xfeatures, xcomp_bv;
	u64				reserved[6];
} __attribute__((packed));


struct xregs_state {
	struct fxregs_state		i387;
	struct xstate_header		header;
	u8				extended_state_area[0];
} __attribute__ ((packed, aligned (64)));

 
union fpregs_state {
	struct fregs_state		fsave;
	struct fxregs_state		fxsave;
	struct swregs_state		soft;
	struct xregs_state		xsave;
	u8 __padding[PAGE_SIZE];
};

struct fpstate {

	u64			xfeatures;


	u64			xfd;


	union fpregs_state	regs;


} __aligned(64);


struct fpu {

	unsigned int			last_cpu;


	struct fpstate			*fpstate;


	struct fpstate			__fpstate;

};


struct fpu_state_config {

	 
	unsigned int		default_size;

	 
	u64 max_features;

	 
	u64 default_features;
};

 
extern struct fpu_state_config fpu_kernel_cfg, fpu_user_cfg;

#endif  
