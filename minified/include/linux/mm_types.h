#ifndef _LINUX_MM_TYPES_H
#define _LINUX_MM_TYPES_H

#ifndef _LINUX_MM_TYPES_TASK_H
#define _LINUX_MM_TYPES_TASK_H

#include <linux/types.h>
#include <linux/threads.h>
#include <linux/atomic.h>
#include <linux/cpumask.h>

#include <asm/page.h>

/* NR_CPUS=1 < CONFIG_SPLIT_PTLOCK_CPUS=4, so USE_SPLIT_*=0 */
#define USE_SPLIT_PTE_PTLOCKS	0
#define USE_SPLIT_PMD_PTLOCKS	0
#define ALLOC_SPLIT_PTLOCKS	0

#define VMACACHE_BITS 2
#define VMACACHE_SIZE (1U << VMACACHE_BITS)
#define VMACACHE_MASK (VMACACHE_SIZE - 1)

struct vmacache {
	u64 seqnum;
	struct vm_area_struct *vmas[VMACACHE_SIZE];
};

#endif /* _LINUX_MM_TYPES_TASK_H */

#define AT_VECTOR_SIZE_ARCH 3
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_ENTRY  9
#ifndef AT_MINSIGSTKSZ
#define AT_MINSIGSTKSZ	51
#endif
#define AT_VECTOR_SIZE_BASE 20
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/completion.h>
#include <linux/cpumask.h>
#include <linux/rcupdate.h>
#include <linux/numa.h>
#include <generated/bounds.h>

#ifndef PAGE_FLAGS_LAYOUT_H
#define PAGE_FLAGS_LAYOUT_H

#if MAX_NR_ZONES < 2
#define ZONES_SHIFT 0
#elif MAX_NR_ZONES <= 2
#define ZONES_SHIFT 1
#elif MAX_NR_ZONES <= 4
#define ZONES_SHIFT 2
#elif MAX_NR_ZONES <= 8
#define ZONES_SHIFT 3
#else
#error ZONES_SHIFT "Too many zones configured"
#endif

#define ZONES_WIDTH		ZONES_SHIFT

#ifndef BUILD_VDSO32_64
#define SECTIONS_WIDTH		0

#if ZONES_WIDTH + SECTIONS_WIDTH + NODES_SHIFT <= BITS_PER_LONG - NR_PAGEFLAGS
#define NODES_WIDTH		NODES_SHIFT
#else
#define NODES_WIDTH		0
#endif

#define KASAN_TAG_WIDTH 0

#define LAST_CPUPID_SHIFT 0

#if ZONES_WIDTH + SECTIONS_WIDTH + NODES_WIDTH + KASAN_TAG_WIDTH + LAST_CPUPID_SHIFT \
	<= BITS_PER_LONG - NR_PAGEFLAGS
#define LAST_CPUPID_WIDTH LAST_CPUPID_SHIFT
#else
#define LAST_CPUPID_WIDTH 0
#endif

#if LAST_CPUPID_SHIFT != 0 && LAST_CPUPID_WIDTH == 0
#define LAST_CPUPID_NOT_IN_PAGE_FLAGS
#endif

#if ZONES_WIDTH + SECTIONS_WIDTH + NODES_WIDTH + KASAN_TAG_WIDTH + LAST_CPUPID_WIDTH \
	> BITS_PER_LONG - NR_PAGEFLAGS
#error "Not enough bits in page flags"
#endif

#endif
#endif /* PAGE_FLAGS_LAYOUT_H */
#include <linux/workqueue.h>
#include <linux/seqlock.h>

#include <asm/mmu.h>

#define AT_VECTOR_SIZE (2*(AT_VECTOR_SIZE_ARCH + AT_VECTOR_SIZE_BASE + 1))

struct address_space;

#define _struct_page_alignment	__aligned(2 * sizeof(unsigned long))

struct page {
	unsigned long flags;		 
	 
	union {
		struct {

			union {
				struct list_head lru;

				struct {

					void *__filler;
				};
			};
			 
			struct address_space *mapping;
			pgoff_t index;		 
			 
			unsigned long private;
		};
		struct {	 
			unsigned long compound_head;	 

			unsigned char compound_dtor;
			unsigned char compound_order;
			atomic_t compound_mapcount;
		};
		struct {
			unsigned long _pt_pad_1;
			unsigned long _pt_pad_2;
			unsigned long _pt_pad_3;
			struct mm_struct *pt_mm;
#if ALLOC_SPLIT_PTLOCKS
			spinlock_t *ptl;
#else
			spinlock_t ptl;
#endif
		};

		struct rcu_head rcu_head;
	};

	union {		 
		 
		atomic_t _mapcount;

		unsigned int page_type;
	};

	atomic_t _refcount;
} _struct_page_alignment;

struct folio {
	 
	union {
		struct {
	 
			unsigned long flags;
			union {
				struct list_head lru;

				struct {
					void *__filler;
				};

			};
			struct address_space *mapping;
			pgoff_t index;
			void *private;
			atomic_t _mapcount;
			atomic_t _refcount;
	 
		};
		struct page page;
	};
};

static_assert(sizeof(struct page) == sizeof(struct folio));
#define FOLIO_MATCH(pg, fl)						\
	static_assert(offsetof(struct page, pg) == offsetof(struct folio, fl))
FOLIO_MATCH(flags, flags);
FOLIO_MATCH(lru, lru);
FOLIO_MATCH(mapping, mapping);
FOLIO_MATCH(compound_head, lru);
FOLIO_MATCH(index, index);
FOLIO_MATCH(private, private);
FOLIO_MATCH(_mapcount, _mapcount);
FOLIO_MATCH(_refcount, _refcount);
#undef FOLIO_MATCH

static inline atomic_t *compound_mapcount_ptr(struct page *page)
{
	return &page[1].compound_mapcount;
}

static inline void set_page_private(struct page *page, unsigned long private)
{
	page->private = private;
}

typedef unsigned long vm_flags_t;

struct vm_area_struct {
	 
	unsigned long vm_start;		 
	unsigned long vm_end;		 

	struct vm_area_struct *vm_next, *vm_prev;

	struct rb_node vm_rb;

	unsigned long rb_subtree_gap;

	struct mm_struct *vm_mm;	 

	pgprot_t vm_page_prot;
	unsigned long vm_flags;

	struct {
		struct rb_node rb;
		unsigned long rb_subtree_last;
	} shared;

	struct list_head anon_vma_chain;
	struct anon_vma *anon_vma;	 

	const struct vm_operations_struct *vm_ops;

	unsigned long vm_pgoff;
	struct file * vm_file;
} __randomize_layout;

struct mm_struct {
	struct {
		struct vm_area_struct *mmap;		 
		struct rb_root mm_rb;
		u64 vmacache_seqnum;                    
		unsigned long (*get_unmapped_area) (struct file *filp,
				unsigned long addr, unsigned long len,
				unsigned long pgoff, unsigned long flags);
		unsigned long mmap_base;	 
		unsigned long mmap_legacy_base;	 
		unsigned long highest_vm_end;	 
		pgd_t * pgd;

		atomic_t mm_users;

		atomic_t mm_count;

		atomic_long_t pgtables_bytes;	 
		int map_count;			 

		spinlock_t page_table_lock;  
		 
		struct rw_semaphore mmap_lock;

		struct list_head mmlist;

		unsigned long total_vm;
		unsigned long def_flags;

		unsigned long saved_auxv[AT_VECTOR_SIZE];  

		struct linux_binfmt *binfmt;

		mm_context_t context;

		unsigned long flags;  

		struct user_namespace *user_ns;

		struct file __rcu *exe_file;
		 
		atomic_t tlb_flush_pending;

	} __randomize_layout;

	unsigned long cpu_bitmap[];
};

extern struct mm_struct init_mm;

static inline cpumask_t *mm_cpumask(struct mm_struct *mm)
{
	return (struct cpumask *)&mm->cpu_bitmap;
}

struct mmu_gather;
extern void tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm);
extern void tlb_gather_mmu_fullmm(struct mmu_gather *tlb, struct mm_struct *mm);
extern void tlb_finish_mmu(struct mmu_gather *tlb);

struct vm_fault;

typedef __bitwise unsigned int vm_fault_t;

enum vm_fault_reason {
	VM_FAULT_OOM            = (__force vm_fault_t)0x000001,
	VM_FAULT_SIGBUS         = (__force vm_fault_t)0x000002,
	VM_FAULT_WRITE          = (__force vm_fault_t)0x000008,
	VM_FAULT_HWPOISON       = (__force vm_fault_t)0x000010,
	VM_FAULT_HWPOISON_LARGE = (__force vm_fault_t)0x000020,
	VM_FAULT_SIGSEGV        = (__force vm_fault_t)0x000040,
	VM_FAULT_NOPAGE         = (__force vm_fault_t)0x000100,
	VM_FAULT_LOCKED         = (__force vm_fault_t)0x000200,
	VM_FAULT_RETRY          = (__force vm_fault_t)0x000400,
	VM_FAULT_FALLBACK       = (__force vm_fault_t)0x000800,
	VM_FAULT_DONE_COW       = (__force vm_fault_t)0x001000,
};

#define VM_FAULT_ERROR (VM_FAULT_OOM | VM_FAULT_SIGBUS |	\
			VM_FAULT_SIGSEGV | VM_FAULT_HWPOISON |	\
			VM_FAULT_HWPOISON_LARGE | VM_FAULT_FALLBACK)

enum fault_flag {
	FAULT_FLAG_WRITE =		1 << 0,
	FAULT_FLAG_MKWRITE =		1 << 1,
	FAULT_FLAG_ALLOW_RETRY =	1 << 2,
	FAULT_FLAG_KILLABLE =		1 << 4,
	FAULT_FLAG_TRIED =		1 << 5,
	FAULT_FLAG_REMOTE =		1 << 7,
	FAULT_FLAG_ORIG_PTE_VALID =	1 << 11,
};

typedef unsigned int __bitwise zap_flags_t;

#endif  
