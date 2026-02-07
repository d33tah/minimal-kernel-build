#ifndef _LINUX_MM_TYPES_H
#define _LINUX_MM_TYPES_H

#include <linux/mm_types_task.h>

#include <asm/auxvec.h>
/* AT_EXECFD removed - unused */
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_BASE   7
#define AT_FLAGS  8
#define AT_ENTRY  9
#define AT_UID    11
#define AT_EUID   12
#define AT_GID    13
#define AT_EGID   14
#define AT_PLATFORM 15
#define AT_HWCAP  16
#define AT_CLKTCK 17
#define AT_SECURE 23
#define AT_BASE_PLATFORM 24
#define AT_RANDOM 25
#define AT_HWCAP2 26
#define AT_EXECFN  31
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
/* uprobes.h inlined */
#define uprobe_get_trap_addr(regs)	instruction_pointer(regs)
#include <linux/rcupdate.h>
#include <linux/page-flags-layout.h>
#include <linux/workqueue.h>
#include <linux/seqlock.h>

#include <asm/mmu.h>

#ifndef AT_VECTOR_SIZE_ARCH
#define AT_VECTOR_SIZE_ARCH 0
#endif
#define AT_VECTOR_SIZE (2*(AT_VECTOR_SIZE_ARCH + AT_VECTOR_SIZE_BASE + 1))

struct address_space;
struct mem_cgroup;

#define _struct_page_alignment	__aligned(2 * sizeof(unsigned long))

struct page {
	unsigned long flags;		 
	 
	union {
		struct {

			union {
				struct list_head lru;

				struct {

					void *__filler;
					/* mlock_count removed - only written, never read */
				};
			};
			 
			struct address_space *mapping;
			pgoff_t index;		 
			 
			unsigned long private;
		};
		/* page_pool sub-struct removed - never used */
		struct {	 
			unsigned long compound_head;	 

			 
			unsigned char compound_dtor;
			unsigned char compound_order;
			atomic_t compound_mapcount;
			/* compound_pincount removed - write-only */
		};
		/* deferred_list sub-struct removed - never used */
		struct {	 
			unsigned long _pt_pad_1;	 
			pgtable_t pmd_huge_pte;  
			unsigned long _pt_pad_2;	 
			union {
				struct mm_struct *pt_mm;  
				atomic_t pt_frag_refcount;  
			};
#if ALLOC_SPLIT_PTLOCKS
			spinlock_t *ptl;
#else
			spinlock_t ptl;
#endif
		};
		/* zone_device sub-struct removed - never used */

		 
		struct rcu_head rcu_head;
	};

	union {		 
		 
		atomic_t _mapcount;

		 
		unsigned int page_type;
	};

	 
	atomic_t _refcount;


	/* WANT_PAGE_VIRTUAL virtual field removed - never defined */

#ifdef LAST_CPUPID_NOT_IN_PAGE_FLAGS
	int _last_cpupid;
#endif
} _struct_page_alignment;

struct folio {
	 
	union {
		struct {
	 
			unsigned long flags;
			union {
				struct list_head lru;

				struct {
					void *__filler;
					/* mlock_count removed - write-only */
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

/* folio_mapcount_ptr inlined into util.c */

static inline atomic_t *compound_mapcount_ptr(struct page *page)
{
	return &page[1].compound_mapcount;
}

/* compound_pincount_ptr removed - write-only (folio_pincount_ptr removed) */

#define page_private(page)		((page)->private)

static inline void set_page_private(struct page *page, unsigned long private)
{
	page->private = private;
}

typedef unsigned long vm_flags_t;

/* NULL_VM_UFFD_CTX, struct vm_userfaultfd_ctx removed - unused */

/* anon_vma_name forward decl and fields removed - never used */

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
	void * vm_private_data;
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
		/* hiwater_rss, hiwater_vm removed - write-only fields */

		unsigned long total_vm;
		unsigned long def_flags;

		/* write_protect_seq, arg_lock removed - initialized but never used */

		/* start_code, end_code, start_data, end_data removed - write-only fields */
		/* start_brk, brk, start_stack removed - write-only fields (brk is COND_SYSCALL stub) */
		/* arg_start, arg_end, env_start, env_end removed - write-only fields */

		unsigned long saved_auxv[AT_VECTOR_SIZE];  

		 
		/* rss_stat removed - write-only (counters never read back) */

		struct linux_binfmt *binfmt;

		 
		mm_context_t context;

		unsigned long flags;  

		struct user_namespace *user_ns;

		 
		struct file __rcu *exe_file;
		 
		atomic_t tlb_flush_pending;

		atomic_t tlb_flush_batched;
		/* async_put_work removed - never scheduled */

	} __randomize_layout;

	 
	unsigned long cpu_bitmap[];
};

extern struct mm_struct init_mm;

/* mm_init_cpumask inlined into fork.c */

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
	/* VM_FAULT_NEEDDSYNC and VM_FAULT_HINDEX_MASK removed - unused */
};

#define VM_FAULT_ERROR (VM_FAULT_OOM | VM_FAULT_SIGBUS |	\
			VM_FAULT_SIGSEGV | VM_FAULT_HWPOISON |	\
			VM_FAULT_HWPOISON_LARGE | VM_FAULT_FALLBACK)

struct vm_special_mapping {
	/* name field removed - never read */
	struct page **pages;
	vm_fault_t (*fault)(const struct vm_special_mapping *sm, struct vm_area_struct *vma,
			    struct vm_fault *vmf);
	/* mremap removed - mremap syscall returns ENOSYS */
};

/* enum tlb_flush_reason removed - never used */

/* swp_entry_t removed - never used */

enum fault_flag {
	FAULT_FLAG_WRITE =		1 << 0,
	FAULT_FLAG_MKWRITE =		1 << 1,
	FAULT_FLAG_ALLOW_RETRY =	1 << 2,
	/* FAULT_FLAG_RETRY_NOWAIT removed - never set */
	FAULT_FLAG_KILLABLE =		1 << 4,
	FAULT_FLAG_TRIED =		1 << 5,
	/* FAULT_FLAG_USER removed - never tested */
	FAULT_FLAG_REMOTE =		1 << 7,
	/* FAULT_FLAG_INSTRUCTION removed - never tested */
	/* FAULT_FLAG_INTERRUPTIBLE removed - never tested */
	/* FAULT_FLAG_UNSHARE removed - never set */
	FAULT_FLAG_ORIG_PTE_VALID =	1 << 11,
};

typedef unsigned int __bitwise zap_flags_t;

#endif  
