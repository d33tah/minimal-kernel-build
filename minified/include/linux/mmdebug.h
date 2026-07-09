#ifndef LINUX_MM_DEBUG_H
#define LINUX_MM_DEBUG_H 1

#include <linux/bug.h>

struct page;
struct vm_area_struct;
struct mm_struct;

#define VM_BUG_ON(cond) BUILD_BUG_ON_INVALID(cond)
#define VM_BUG_ON_PAGE(cond, page) VM_BUG_ON(cond)
#define VM_BUG_ON_FOLIO(cond, folio) VM_BUG_ON(cond)
#define VM_BUG_ON_VMA(cond, vma) VM_BUG_ON(cond)
#define VM_BUG_ON_MM(cond, mm) VM_BUG_ON(cond)
#define VM_WARN_ON(cond) BUILD_BUG_ON_INVALID(cond)
#define VM_WARN_ON_ONCE(cond) BUILD_BUG_ON_INVALID(cond)

#define VM_BUG_ON_PGFLAGS(cond, page) BUILD_BUG_ON_INVALID(cond)

#endif
