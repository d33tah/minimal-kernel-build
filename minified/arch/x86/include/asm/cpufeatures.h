
#ifndef _ASM_X86_CPUFEATURES_H
#define _ASM_X86_CPUFEATURES_H

#define NCAPINTS			20
#define NBUGINTS			1

/* Word 0 - Intel flags */
#define X86_FEATURE_VME			( 0*32+ 1)
#define X86_FEATURE_DE			( 0*32+ 2)
#define X86_FEATURE_PSE			( 0*32+ 3)
#define X86_FEATURE_TSC			( 0*32+ 4)
#define X86_FEATURE_PGE			( 0*32+13)

#define X86_FEATURE_HYPERVISOR		( 4*32+31)

/* Word 9 - Extended leaf 7 */
#define X86_FEATURE_FSGSBASE		( 9*32+ 0)
#define X86_FEATURE_SMEP		( 9*32+ 7)

#endif
