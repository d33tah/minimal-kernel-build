#ifndef _LINUX_PKEYS_H
#define _LINUX_PKEYS_H

#include <linux/mm.h>

#define ARCH_DEFAULT_PKEY	0

#define arch_max_pkey() (1)
#define execute_only_pkey(mm) (0)
#define ARCH_VM_PKEY_FLAGS 0

static inline int vma_pkey(struct vm_area_struct *vma)
{
	return 0;
}

#endif  
