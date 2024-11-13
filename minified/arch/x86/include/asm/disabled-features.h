#ifndef _ASM_X86_DISABLED_FEATURES_H
#define _ASM_X86_DISABLED_FEATURES_H

/* These features, although they might be available in a CPU
 * will not be used because the compile options to support
 * them are not present.
 *
 * This code allows them to be checked and disabled at
 * compile time without an explicit #ifdef.  Use
 * cpu_feature_enabled().
 */

# define DISABLE_UMIP	(1<<(X86_FEATURE_UMIP & 31))

# define DISABLE_VME		0
# define DISABLE_K6_MTRR	0
# define DISABLE_CYRIX_ARR	0
# define DISABLE_CENTAUR_MCR	0
# define DISABLE_PCID		(1<<(X86_FEATURE_PCID & 31))

# define DISABLE_PKU		(1<<(X86_FEATURE_PKU & 31))
# define DISABLE_OSPKE		(1<<(X86_FEATURE_OSPKE & 31))

# define DISABLE_LA57	(1<<(X86_FEATURE_LA57 & 31))

# define DISABLE_PTI		(1 << (X86_FEATURE_PTI & 31))

# define DISABLE_RETPOLINE	((1 << (X86_FEATURE_RETPOLINE & 31)) | \
				 (1 << (X86_FEATURE_RETPOLINE_LFENCE & 31)))

# define DISABLE_RETHUNK	(1 << (X86_FEATURE_RETHUNK & 31))

# define DISABLE_UNRET		(1 << (X86_FEATURE_UNRET & 31))

# define DISABLE_ENQCMD		(1 << (X86_FEATURE_ENQCMD & 31))

# define DISABLE_SGX	(1 << (X86_FEATURE_SGX & 31))

# define DISABLE_TDX_GUEST	(1 << (X86_FEATURE_TDX_GUEST & 31))

/*
 * Make sure to add features to the correct mask
 */
#define DISABLED_MASK0	(DISABLE_VME)
#define DISABLED_MASK1	0
#define DISABLED_MASK2	0
#define DISABLED_MASK3	(DISABLE_CYRIX_ARR|DISABLE_CENTAUR_MCR|DISABLE_K6_MTRR)
#define DISABLED_MASK4	(DISABLE_PCID)
#define DISABLED_MASK5	0
#define DISABLED_MASK6	0
#define DISABLED_MASK7	(DISABLE_PTI)
#define DISABLED_MASK8	(DISABLE_TDX_GUEST)
#define DISABLED_MASK9	(DISABLE_SGX)
#define DISABLED_MASK10	0
#define DISABLED_MASK11	(DISABLE_RETPOLINE|DISABLE_RETHUNK|DISABLE_UNRET)
#define DISABLED_MASK12	0
#define DISABLED_MASK13	0
#define DISABLED_MASK14	0
#define DISABLED_MASK15	0
#define DISABLED_MASK16	(DISABLE_PKU|DISABLE_OSPKE|DISABLE_LA57|DISABLE_UMIP| \
			 DISABLE_ENQCMD)
#define DISABLED_MASK17	0
#define DISABLED_MASK18	0
#define DISABLED_MASK19	0
#define DISABLED_MASK_CHECK BUILD_BUG_ON_ZERO(NCAPINTS != 20)

#endif /* _ASM_X86_DISABLED_FEATURES_H */
