/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CPUFEATURES_H
#define _ASM_X86_CPUFEATURES_H

/*
 * Defines x86 CPU feature bits
 */
#define NCAPINTS 20 /* N 32-bit words worth of info */
#define NBUGINTS 1 /* N 32-bit bug flags */

/* Word 0 - Intel flags */
#define X86_FEATURE_VME (0 * 32 + 1) /* Virtual Mode Extensions */
#define X86_FEATURE_DE (0 * 32 + 2) /* Debugging Extensions */
#define X86_FEATURE_PSE (0 * 32 + 3) /* Page Size Extensions */
#define X86_FEATURE_TSC (0 * 32 + 4) /* Time Stamp Counter */
#define X86_FEATURE_PGE (0 * 32 + 13) /* Page Global Enable */

#define X86_FEATURE_HYPERVISOR (4 * 32 + 31) /* Running on a hypervisor */

/* Word 9 - Extended leaf 7 */
#define X86_FEATURE_FSGSBASE (9 * 32 + 0)
#define X86_FEATURE_SMEP (9 * 32 + 7) /* Supervisor Mode Execution Protection */

#endif
