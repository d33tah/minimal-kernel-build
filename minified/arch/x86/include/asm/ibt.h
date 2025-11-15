 
#ifndef _ASM_X86_IBT_H
#define _ASM_X86_IBT_H

#include <linux/types.h>

 

#define HAS_KERNEL_IBT	0

#ifndef __ASSEMBLY__

#define ASM_ENDBR

#define __noendbr

static inline bool is_endbr(u32 val) { return false; }

static inline u64 ibt_save(void) { return 0; }
static inline void ibt_restore(u64 save) { }

#else  

#define ENDBR

#endif  


#define ENDBR_INSN_SIZE		(4*HAS_KERNEL_IBT)

#endif  
