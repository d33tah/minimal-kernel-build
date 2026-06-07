 
#ifndef _ASM_X86_PARAVIRT_H
#define _ASM_X86_PARAVIRT_H
 

# define default_banner x86_init_noop

#ifndef __ASSEMBLY__
/* paravirt_arch_dup_mmap removed - unused */

static inline void paravirt_arch_exit_mmap(struct mm_struct *mm)
{
}

/* paravirt_set_cap removed - unused */
#endif
#endif  
