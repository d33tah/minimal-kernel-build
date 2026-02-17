 
#ifndef _ASM_X86_FPU_H
#define _ASM_X86_FPU_H

struct fregs_state {
	u32			cwd;	 
	u32			swd;	 
	u32			twd;	 
	u32			fip;	 
	u32			fcs;	 
	u32			foo;	 
	u32			fos;	 

	u32			st_space[20];

	u32			status;
};

struct fxregs_state {
	u16			cwd;  
	u16			swd;  
	u16			twd;  
	u16			fop;  
	union {
		struct {
			u64	rip;  
			u64	rdp;  
		};
		struct {
			u32	fip;  
			u32	fcs;  
			u32	foo;  
			u32	fos;  
		};
	};
	u32			mxcsr;		 
	u32			mxcsr_mask;	 

	u32			st_space[32];

	u32			xmm_space[64];

	u32			padding[12];

	union {
		u32		padding1[12];
		u32		sw_reserved[12];
	};

} __attribute__((aligned(16)));

#define MXCSR_DEFAULT		0x1f80

#define XFEATURE_MASK_FP		0x001
#define XFEATURE_MASK_SSE		0x002
#define XFEATURE_MASK_FPSSE		0x003
#define XFEATURE_MASK_PASID		0x400
#define XFEATURE_MASK_XTILE_DATA	0x40000

struct xstate_header {
	u64				xfeatures;
	u64				xcomp_bv;
	u64				reserved[6];
} __attribute__((packed));

#define XCOMP_BV_COMPACTED_FORMAT ((u64)1 << 63)

struct xregs_state {
	struct fxregs_state		i387;
	struct xstate_header		header;
	u8				extended_state_area[0];
} __attribute__ ((packed, aligned (64)));

union fpregs_state {
	struct fregs_state		fsave;
	struct fxregs_state		fxsave;
	struct xregs_state		xsave;
	u8 __padding[256];  /* Reduced from PAGE_SIZE for minimal boot */
};

struct fpstate {
	 
	unsigned int		size;

	unsigned int		user_size;

	u64			xfeatures;

	union fpregs_state	regs;

} __aligned(64);

struct fpu {
	unsigned int			last_cpu;

	struct fpstate			*fpstate;

	struct fpstate			__fpstate;
};

struct fpu_state_config {
	 
	unsigned int		max_size;

	unsigned int		default_size;

	u64 max_features;

	u64 default_features;
	 
	u64 legacy_features;
};

extern struct fpu_state_config fpu_kernel_cfg, fpu_user_cfg;

#endif  
