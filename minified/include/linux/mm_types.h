 
#ifndef _LINUX_MM_TYPES_H
#define _LINUX_MM_TYPES_H

#include <linux/mm_types_task.h>

#include <linux/auxvec.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/completion.h>
#include <linux/cpumask.h>
#include <linux/uprobes.h>
#include <linux/rcupdate.h>
#include <linux/page-flags-layout.h>
#include <linux/workqueue.h>
#include <linux/seqlock.h>

#include <asm/mmu.h>

#ifndef AT_VECTOR_SIZE_ARCH
#define AT_VECTOR_SIZE_ARCH 0
#endif
#define AT_VECTOR_SIZE (2*(AT_VECTOR_SIZE_ARCH + AT_VECTOR_SIZE_BASE + 1))

#define INIT_PASID	0

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
					 
					unsigned int mlock_count;
				};
			};
			 
			struct address_space *mapping;
			pgoff_t index;		 
			 
			unsigned long private;
		};
		struct {	 
			 
			unsigned long pp_magic;
			struct page_pool *pp;
			unsigned long _pp_mapping_pad;
			unsigned long dma_addr;
			union {
				 
				unsigned long dma_addr_upper;
				 
				atomic_long_t pp_frag_count;
			};
		};
		struct {	 
			unsigned long compound_head;	 

			 
			unsigned char compound_dtor;
			unsigned char compound_order;
			atomic_t compound_mapcount;
			atomic_t compound_pincount;
		};
		struct {	 
			unsigned long _compound_pad_1;	 
			unsigned long _compound_pad_2;
			 
			struct list_head deferred_list;
		};
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
		struct {	 
			 
			struct dev_pagemap *pgmap;
			void *zone_device_data;
			 
		};

		 
		struct rcu_head rcu_head;
	};

	union {		 
		 
		atomic_t _mapcount;

		 
		unsigned int page_type;
	};

	 
	atomic_t _refcount;


	 
#if defined(WANT_PAGE_VIRTUAL)
	void *virtual;			 
#endif  

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
	 
					unsigned int mlock_count;
	 
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

static inline atomic_t *folio_mapcount_ptr(struct folio *folio)
{
	struct page *tail = &folio->page + 1;
	return &tail->compound_mapcount;
}

static inline atomic_t *compound_mapcount_ptr(struct page *page)
{
	return &page[1].compound_mapcount;
}

static inline atomic_t *compound_pincount_ptr(struct page *page)
{
	return &page[1].compound_pincount;
}

 
#define STRUCT_PAGE_MAX_SHIFT	(order_base_2(sizeof(struct page)))

#define PAGE_FRAG_CACHE_MAX_SIZE	__ALIGN_MASK(32768, ~PAGE_MASK)
#define PAGE_FRAG_CACHE_MAX_ORDER	get_order(PAGE_FRAG_CACHE_MAX_SIZE)

 
#define page_private(page)		((page)->private)

static inline void set_page_private(struct page *page, unsigned long private)
{
	page->private = private;
}

struct page_frag_cache {
	void * va;
#if (PAGE_SIZE < PAGE_FRAG_CACHE_MAX_SIZE)
	__u16 offset;
	__u16 size;
#else
	__u32 offset;
#endif
	 
	unsigned int		pagecnt_bias;
	bool pfmemalloc;
};

typedef unsigned long vm_flags_t;

 
struct vm_region {
	struct rb_node	vm_rb;		 
	vm_flags_t	vm_flags;	 
	unsigned long	vm_start;	 
	unsigned long	vm_end;		 
	unsigned long	vm_top;		 
	unsigned long	vm_pgoff;	 
	struct file	*vm_file;	 

	int		vm_usage;	 
	bool		vm_icache_flushed : 1;  
};

#define NULL_VM_UFFD_CTX ((struct vm_userfaultfd_ctx) {})
struct vm_userfaultfd_ctx {};

struct anon_vma_name {
	struct kref kref;
	 
	char name[];
};

 
struct vm_area_struct {
	 

	unsigned long vm_start;		 
	unsigned long vm_end;		 

	 
	struct vm_area_struct *vm_next, *vm_prev;

	struct rb_node vm_rb;

	 
	unsigned long rb_subtree_gap;

	 

	struct mm_struct *vm_mm;	 

	 
	pgprot_t vm_page_prot;
	unsigned long vm_flags;		 

	 

	union {
		struct {
			struct rb_node rb;
			unsigned long rb_subtree_last;
		} shared;
		 
		struct anon_vma_name *anon_name;
	};

	 
	struct list_head anon_vma_chain;  
	struct anon_vma *anon_vma;	 

	 
	const struct vm_operations_struct *vm_ops;

	 
	unsigned long vm_pgoff;		 
	struct file * vm_file;		 
	void * vm_private_data;		 

	struct vm_userfaultfd_ctx vm_userfaultfd_ctx;
} __randomize_layout;

struct kioctx_table;
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
		unsigned long task_size;	 
		unsigned long highest_vm_end;	 
		pgd_t * pgd;


		 
		atomic_t mm_users;

		 
		atomic_t mm_count;

		atomic_long_t pgtables_bytes;	 
		int map_count;			 

		spinlock_t page_table_lock;  
		 
		struct rw_semaphore mmap_lock;

		struct list_head mmlist;  


		unsigned long hiwater_rss;  
		unsigned long hiwater_vm;   

		unsigned long total_vm;	    
		unsigned long locked_vm;    
		atomic64_t    pinned_vm;    
		unsigned long data_vm;	    
		unsigned long exec_vm;	    
		unsigned long stack_vm;	    
		unsigned long def_flags;

		 
		seqcount_t write_protect_seq;

		spinlock_t arg_lock;  

		unsigned long start_code, end_code, start_data, end_data;
		unsigned long start_brk, brk, start_stack;
		unsigned long arg_start, arg_end, env_start, env_end;

		unsigned long saved_auxv[AT_VECTOR_SIZE];  

		 
		struct mm_rss_stat rss_stat;

		struct linux_binfmt *binfmt;

		 
		mm_context_t context;

		unsigned long flags;  

		struct user_namespace *user_ns;

		 
		struct file __rcu *exe_file;
		 
		atomic_t tlb_flush_pending;
		 
		atomic_t tlb_flush_batched;
		struct uprobes_state uprobes_state;
		struct work_struct async_put_work;

	} __randomize_layout;

	 
	unsigned long cpu_bitmap[];
};

extern struct mm_struct init_mm;

 
static inline void mm_init_cpumask(struct mm_struct *mm)
{
	unsigned long cpu_bitmap = (unsigned long)mm;

	cpu_bitmap += offsetof(struct mm_struct, cpu_bitmap);
	cpumask_clear((struct cpumask *)cpu_bitmap);
}

 
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
	VM_FAULT_MAJOR          = (__force vm_fault_t)0x000004,
	VM_FAULT_WRITE          = (__force vm_fault_t)0x000008,
	VM_FAULT_HWPOISON       = (__force vm_fault_t)0x000010,
	VM_FAULT_HWPOISON_LARGE = (__force vm_fault_t)0x000020,
	VM_FAULT_SIGSEGV        = (__force vm_fault_t)0x000040,
	VM_FAULT_NOPAGE         = (__force vm_fault_t)0x000100,
	VM_FAULT_LOCKED         = (__force vm_fault_t)0x000200,
	VM_FAULT_RETRY          = (__force vm_fault_t)0x000400,
	VM_FAULT_FALLBACK       = (__force vm_fault_t)0x000800,
	VM_FAULT_DONE_COW       = (__force vm_fault_t)0x001000,
	VM_FAULT_NEEDDSYNC      = (__force vm_fault_t)0x002000,
	VM_FAULT_HINDEX_MASK    = (__force vm_fault_t)0x0f0000,
};

 
#define VM_FAULT_SET_HINDEX(x) ((__force vm_fault_t)((x) << 16))
#define VM_FAULT_GET_HINDEX(x) (((__force unsigned int)(x) >> 16) & 0xf)

#define VM_FAULT_ERROR (VM_FAULT_OOM | VM_FAULT_SIGBUS |	\
			VM_FAULT_SIGSEGV | VM_FAULT_HWPOISON |	\
			VM_FAULT_HWPOISON_LARGE | VM_FAULT_FALLBACK)

#define VM_FAULT_RESULT_TRACE \
	{ VM_FAULT_OOM,                 "OOM" },	\
	{ VM_FAULT_SIGBUS,              "SIGBUS" },	\
	{ VM_FAULT_MAJOR,               "MAJOR" },	\
	{ VM_FAULT_WRITE,               "WRITE" },	\
	{ VM_FAULT_HWPOISON,            "HWPOISON" },	\
	{ VM_FAULT_HWPOISON_LARGE,      "HWPOISON_LARGE" },	\
	{ VM_FAULT_SIGSEGV,             "SIGSEGV" },	\
	{ VM_FAULT_NOPAGE,              "NOPAGE" },	\
	{ VM_FAULT_LOCKED,              "LOCKED" },	\
	{ VM_FAULT_RETRY,               "RETRY" },	\
	{ VM_FAULT_FALLBACK,            "FALLBACK" },	\
	{ VM_FAULT_DONE_COW,            "DONE_COW" },	\
	{ VM_FAULT_NEEDDSYNC,           "NEEDDSYNC" }

struct vm_special_mapping {
	const char *name;	 

	 
	struct page **pages;

	 
	vm_fault_t (*fault)(const struct vm_special_mapping *sm,
				struct vm_area_struct *vma,
				struct vm_fault *vmf);

	int (*mremap)(const struct vm_special_mapping *sm,
		     struct vm_area_struct *new_vma);
};

enum tlb_flush_reason {
	TLB_FLUSH_ON_TASK_SWITCH,
	TLB_REMOTE_SHOOTDOWN,
	TLB_LOCAL_SHOOTDOWN,
	TLB_LOCAL_MM_SHOOTDOWN,
	TLB_REMOTE_SEND_IPI,
	NR_TLB_FLUSH_REASONS,
};

  
typedef struct {
	unsigned long val;
} swp_entry_t;

 
enum fault_flag {
	FAULT_FLAG_WRITE =		1 << 0,
	FAULT_FLAG_MKWRITE =		1 << 1,
	FAULT_FLAG_ALLOW_RETRY =	1 << 2,
	FAULT_FLAG_RETRY_NOWAIT = 	1 << 3,
	FAULT_FLAG_KILLABLE =		1 << 4,
	FAULT_FLAG_TRIED = 		1 << 5,
	FAULT_FLAG_USER =		1 << 6,
	FAULT_FLAG_REMOTE =		1 << 7,
	FAULT_FLAG_INSTRUCTION =	1 << 8,
	FAULT_FLAG_INTERRUPTIBLE =	1 << 9,
	FAULT_FLAG_UNSHARE =		1 << 10,
	FAULT_FLAG_ORIG_PTE_VALID =	1 << 11,
};

typedef unsigned int __bitwise zap_flags_t;

#endif  
