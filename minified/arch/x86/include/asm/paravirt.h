 
#ifndef _ASM_X86_PARAVIRT_H
#define _ASM_X86_PARAVIRT_H
 

# define default_banner x86_init_noop

#ifndef __ASSEMBLY__
static inline void paravirt_arch_dup_mmap(struct mm_struct *oldmm,
					  struct mm_struct *mm)
{
}

static inline void paravirt_arch_exit_mmap(struct mm_struct *mm)
{
}

static inline void paravirt_set_cap(void)
{
}
#endif  
#endif  
