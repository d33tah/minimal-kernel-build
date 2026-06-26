 
#ifndef _ASM_X86_SWAB_H
#define _ASM_X86_SWAB_H

#include <linux/types.h>
#include <linux/compiler.h>

/*
 * Dead arch swab inlines removed: generic uapi/linux/swab.h uses
 * __builtin_bswap32/64 directly and only consumes the __arch_swabNp/__arch_swabNs
 * variants (which this arch does not define). __arch_swab32/__arch_swab64 had
 * 0 callers and 0 #ifdef probes tree-wide.
 */

#endif
