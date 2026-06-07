
#ifndef _ASM_X86_CPUFEATURES_H
#define _ASM_X86_CPUFEATURES_H

/* required-features.h inlined - only FPU is required */
#define REQUIRED_MASK0	(1<<(X86_FEATURE_FPU & 31))
#define SSE_MASK	0
#define REQUIRED_MASK1	0
#define REQUIRED_MASK2	0
#define REQUIRED_MASK3	0
#define REQUIRED_MASK4	0
#define REQUIRED_MASK5	0
#define REQUIRED_MASK6	0
#define REQUIRED_MASK7	0
#define REQUIRED_MASK8	0
#define REQUIRED_MASK9	0
#define REQUIRED_MASK10	0
#define REQUIRED_MASK11	0
#define REQUIRED_MASK12	0
#define REQUIRED_MASK13	0
#define REQUIRED_MASK14	0
#define REQUIRED_MASK15	0
#define REQUIRED_MASK16	0
#define REQUIRED_MASK17	0
#define REQUIRED_MASK18	0
#define REQUIRED_MASK19	0
#define REQUIRED_MASK_CHECK BUILD_BUG_ON_ZERO(NCAPINTS != 20)

# define DISABLE_VME		0
# define DISABLE_PCID		(1<<(X86_FEATURE_PCID & 31))
# define DISABLE_OSPKE		(1<<(X86_FEATURE_OSPKE & 31))
# define DISABLE_LA57	(1<<(X86_FEATURE_LA57 & 31))
# define DISABLE_PTI		(1 << (X86_FEATURE_PTI & 31))
#define DISABLED_MASK0	(DISABLE_VME)
#define DISABLED_MASK1	0
#define DISABLED_MASK2	0
#define DISABLED_MASK3	0
#define DISABLED_MASK4	(DISABLE_PCID)
#define DISABLED_MASK5	0
#define DISABLED_MASK6	0
#define DISABLED_MASK7	(DISABLE_PTI)
#define DISABLED_MASK8	0
#define DISABLED_MASK9	0
#define DISABLED_MASK10	0
#define DISABLED_MASK11	0
#define DISABLED_MASK12	0
#define DISABLED_MASK13	0
#define DISABLED_MASK14	0
#define DISABLED_MASK15	0
#define DISABLED_MASK16	(DISABLE_OSPKE|DISABLE_LA57)
#define DISABLED_MASK17	0
#define DISABLED_MASK18	0
#define DISABLED_MASK19	0
#define DISABLED_MASK_CHECK BUILD_BUG_ON_ZERO(NCAPINTS != 20)

#define NCAPINTS			20
#define NBUGINTS			1

/* Word 0 - Intel flags */
#define X86_FEATURE_FPU			( 0*32+ 0)
#define X86_FEATURE_VME			( 0*32+ 1)
#define X86_FEATURE_DE			( 0*32+ 2)
#define X86_FEATURE_PSE			( 0*32+ 3)
#define X86_FEATURE_TSC			( 0*32+ 4)
#define X86_FEATURE_PGE			( 0*32+13)
#define X86_FEATURE_FXSR		( 0*32+24)
#define X86_FEATURE_XMM			( 0*32+25)
#define X86_FEATURE_XMM2		( 0*32+26)

/* Word 1 - AMD/extended flags */
#define X86_FEATURE_NX			( 1*32+20)
#define X86_FEATURE_RDTSCP		( 1*32+27)
#define X86_FEATURE_LM			( 1*32+29)

/* Word 3 - Auxiliary flags */
#define X86_FEATURE_LFENCE_RDTSC	( 3*32+18)
#define X86_FEATURE_NOPL		( 3*32+20)
#define X86_FEATURE_ALWAYS		( 3*32+21)
#define X86_FEATURE_TSC_RELIABLE	( 3*32+23)
#define X86_FEATURE_TSC_KNOWN_FREQ	( 3*32+31)

/* Word 4 - Intel extended */
#define X86_FEATURE_PCID		( 4*32+17)
#define X86_FEATURE_XSAVE		( 4*32+26)

/* Word 6 - AMD extended */
#define X86_FEATURE_3DNOWPREFETCH	( 6*32+ 8)

/* Word 7 - Kernel-synthesized flags */
#define X86_FEATURE_XCOMPACTED		( 7*32+10)
#define X86_FEATURE_PTI			( 7*32+11)
#define X86_FEATURE_RSB_CTXSW		( 7*32+19)

/* Word 8 - Virtualization flags */
#define X86_FEATURE_XENPV		( 8*32+16)

/* Word 9 - Extended leaf 7 */
#define X86_FEATURE_INVPCID		( 9*32+10)
#define X86_FEATURE_SMAP		( 9*32+20)

/* Word 10 - XSAVE leaf */
#define X86_FEATURE_XSAVEOPT		(10*32+ 0)
#define X86_FEATURE_XSAVEC		(10*32+ 1)
#define X86_FEATURE_XSAVES		(10*32+ 3)

/* Word 16 - Intel extended 2 */
#define X86_FEATURE_OSPKE		(16*32+ 4)
#define X86_FEATURE_LA57		(16*32+16)

/* CPU bugs - only keep the used ones */
#define X86_BUG(x)			(NCAPINTS*32 + (x))

#define X86_BUG_FXSAVE_LEAK		X86_BUG(6)
#define X86_BUG_ESPFIX			X86_BUG(9)

#endif
