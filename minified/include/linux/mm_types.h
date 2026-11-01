/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MM_TYPES_H
#define _LINUX_MM_TYPES_H

#include <linux/auxvec.h>
#include <linux/jiffies.h>

#include <linux/cpumask.h>
#include <linux/mutex.h>

#include <linux/rwsem.h>

#define AT_VECTOR_SIZE (2 * (AT_VECTOR_SIZE_ARCH + AT_VECTOR_SIZE_BASE + 1))

struct address_space;

#define _struct_page_alignment __aligned(2 * sizeof(unsigned long))

struct page {
	unsigned long flags; /* Atomic flags, some possibly
					 * updated asynchronously */
	/*
	 * Five words (20/40 bytes) are available in this union.
	 * WARNING: bit 0 of the first word is used for PageTail(). That
	 * means the other users of this union MUST NOT use the bit to
	 * avoid collision and false-positive PageTail().
	 */
	union {
		struct { /* Page cache and anonymous pages */
			struct list_head lru;
			/* See page-flags.h for PAGE_MAPPING_FLAGS */
			struct address_space *mapping;
			pgoff_t index; /* Our offset within mapping. */
			/**
			 * @private: Mapping-private opaque data.
			 * Usually used for buffer_heads if PagePrivate.
			 * Used for swp_entry_t if PageSwapCache.
			 * Indicates order in the buddy system if PageBuddy.
			 */
			unsigned long private;
		};
		struct { /* page_pool used by netstack */
			unsigned long compound_head; /* Bit zero is set */
			unsigned char compound_order;
			atomic_t compound_mapcount;
			atomic_t compound_pincount;
			/*
			 * ZONE_DEVICE private pages are counted as being
			 * mapped so the next 3 words hold the mapping, index,
			 * and private fields from the source anonymous or
			 * page cache page while the page is migrated to device
			 * private memory.
			 * ZONE_DEVICE MEMORY_DEVICE_FS_DAX pages also
			 * use the mapping, index, and private fields when
			 * pmem backed DAX files are mapped.
			 */
		};

		/** @rcu_head: You can use this to free a page by RCU. */
		struct rcu_head rcu_head;
	};
	union { /* This union is 4 bytes in size. */
		/*
		 * If the page can be mapped to userspace, encodes the number
		 * of times this page is referenced by a page table.
		 */
		atomic_t _mapcount;

		/*
		 * If the page is neither PageSlab nor mappable to userspace,
		 * the value stored here may help determine what this page
		 * is used for.  See page-flags.h for a list of page types
		 * which are currently stored here.
		 */
		unsigned int page_type;
	};

	/* Usage count. *DO NOT USE DIRECTLY*. See page_ref.h */
	atomic_t _refcount;
} _struct_page_alignment;

/**
 * struct folio - Represents a contiguous set of bytes.
 * @flags: Identical to the page flags.
 * @lru: Least Recently Used list; tracks how recently this folio was used.
 * @mlock_count: Number of times this folio has been pinned by mlock().
 * @mapping: The file this page belongs to, or refers to the anon_vma for
 *    anonymous memory.
 * @index: Offset within the file, in units of pages.  For anonymous memory,
 *    this is the index from the beginning of the mmap.
 * @private: Filesystem per-folio data (see folio_attach_private()).
 *    Used for swp_entry_t if folio_test_swapcache().
 * @_mapcount: Do not access this member directly.  Use folio_mapcount() to
 *    find out how many times this folio is mapped by userspace.
 * @_refcount: Do not access this member directly.  Use folio_ref_count()
 *    to find how many references there are to this folio.
 * @memcg_data: Memory Control Group data.
 *
 * A folio is a physically, virtually and logically contiguous set
 * of bytes.  It is a power-of-two in size, and it is aligned to that
 * same power-of-two.  It is at least as large as %PAGE_SIZE.  If it is
 * in the page cache, it is at a file offset which is a multiple of that
 * power-of-two.  It may be mapped into userspace at an address which is
 * at an arbitrary page offset, but its kernel virtual address is aligned
 * to its size.
 */
struct folio {
	/* private: don't document the anon union */
	union {
		struct {
			/* public: */
			unsigned long flags;
			struct list_head lru;
			struct address_space *mapping;
			pgoff_t index;
			void *private;
			atomic_t _mapcount;
			atomic_t _refcount;
			/* private: the union with struct page is transitional */
		};
		struct page page;
	};
};

static_assert(sizeof(struct page) == sizeof(struct folio));
#define FOLIO_MATCH(pg, fl) \
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

static inline atomic_t *compound_pincount_ptr(struct page *page)
{
	return &page[1].compound_pincount;
}

/*
 * page_private can be used on tail pages.  However, PagePrivate is only
 * checked by the VM on the head page.  So page_private on the tail pages
 * should be used for data that's ancillary to the head page (eg attaching
 * buffer heads to tail pages after attaching buffer heads to the head page)
 */
#define page_private(page) ((page)->private)

static inline void set_page_private(struct page *page, unsigned long private)
{
	page->private = private;
}

typedef unsigned long vm_flags_t;

/*
 * This struct describes a virtual memory area. There is one of these
 * per VM-area/task. A VM area is any part of the process virtual memory
 * space that has a special rule for the page-fault handlers (ie a shared
 * library, the executable area etc).
 */
struct vm_area_struct {
	/* The first cache line has the info for VMA tree walking. */

	unsigned long vm_start; /* Our start address within vm_mm. */
	unsigned long vm_end; /* The first byte after our end address
					   within vm_mm. */

	/* linked list of VM areas per task, sorted by address */
	struct vm_area_struct *vm_next, *vm_prev;

	/* Second cache line starts here. */

	struct mm_struct *vm_mm; /* The address space we belong to. */

	/*
	 * Access permissions of this VMA.
	 * See vmf_insert_mixed_prot() for discussion.
	 */
	pgprot_t vm_page_prot;
	unsigned long vm_flags; /* Flags, see mm.h. */

	/* Function pointers to deal with this struct. */
	const struct vm_operations_struct *vm_ops;

	/* Information about our backing store: */
	unsigned long vm_pgoff; /* Offset (within vm_file) in PAGE_SIZE
					   units */
	struct file *vm_file; /* File we map to (can be NULL). */
} __randomize_layout;

struct mm_struct {
	struct {
		struct vm_area_struct *mmap; /* list of VMAs */
		pgd_t *pgd;

		/**
		 * @mm_users: The number of users including userspace.
		 *
		 * Use mmget()/mmget_not_zero()/mmput() to modify. When this
		 * drops to 0 (i.e. when the task exits and there are no other
		 * temporary reference holders), we also release a reference on
		 * @mm_count (which may then free the &struct mm_struct if
		 * @mm_count also drops to 0).
		 */
		atomic_t mm_users;

		/**
		 * @mm_count: The number of references to &struct mm_struct
		 * (@mm_users count as 1).
		 *
		 * Use mmgrab()/mmdrop() to modify. When this drops to 0, the
		 * &struct mm_struct is freed.
		 */
		atomic_t mm_count;
		int map_count; /* number of VMAs */
		spinlock_t page_table_lock; /* Protects page tables and some
					     * counters
					     */
		/*
		 * With some kernel config, the current mmap_lock's offset
		 * inside 'mm_struct' is at 0x120, which is very optimal, as
		 * its two hot fields 'count' and 'owner' sit in 2 different
		 * cachelines,  and when mmap_lock is highly contended, both
		 * of the 2 fields will be accessed frequently, current layout
		 * will help to reduce cache bouncing.
		 *
		 * So please be careful with adding new fields before
		 * mmap_lock, which can easily push the 2 fields into one
		 * cacheline.
		 */
		struct rw_semaphore mmap_lock;
		unsigned long def_flags;
		unsigned long brk;
		unsigned long arg_start;
		unsigned long saved_auxv[AT_VECTOR_SIZE]; /* for /proc/PID/auxv */
		unsigned long flags;
		struct user_namespace *user_ns;
	} __randomize_layout;
};

extern struct mm_struct init_mm;

/**
 * typedef vm_fault_t - Return type for page fault handlers.
 *
 * Page fault handlers return a bitmask of %VM_FAULT values.
 */
typedef __bitwise unsigned int vm_fault_t;

/**
 * enum vm_fault_reason - Page fault handlers return a bitmask of
 * these values to tell the core VM what happened when handling the
 * fault. Used to decide whether a process gets delivered SIGBUS or
 * just gets major/minor fault counters bumped up.
 *
 * @VM_FAULT_OOM:		Out Of Memory
 * @VM_FAULT_SIGBUS:		Bad access
 * @VM_FAULT_MAJOR:		Page read from storage
 * @VM_FAULT_WRITE:		Special case for get_user_pages
 * @VM_FAULT_HWPOISON:		Hit poisoned small page
 * @VM_FAULT_HWPOISON_LARGE:	Hit poisoned large page. Index encoded
 *				in upper bits
 * @VM_FAULT_SIGSEGV:		segmentation fault
 * @VM_FAULT_NOPAGE:		->fault installed the pte, not return page
 * @VM_FAULT_LOCKED:		->fault locked the returned page
 * @VM_FAULT_RETRY:		->fault blocked, must retry
 * @VM_FAULT_FALLBACK:		huge page fault failed, fall back to small
 * @VM_FAULT_DONE_COW:		->fault has fully handled COW
 * @VM_FAULT_NEEDDSYNC:		->fault did not modify page tables and needs
 *				fsync() to complete (for synchronous page faults
 *				in DAX)
 * @VM_FAULT_HINDEX_MASK:	mask HINDEX value
 *
 */
enum vm_fault_reason {
	VM_FAULT_OOM = (__force vm_fault_t)0x000001,
	VM_FAULT_SIGBUS = (__force vm_fault_t)0x000002,
	VM_FAULT_HWPOISON = (__force vm_fault_t)0x000010,
	VM_FAULT_HWPOISON_LARGE = (__force vm_fault_t)0x000020,
	VM_FAULT_SIGSEGV = (__force vm_fault_t)0x000040,
	VM_FAULT_NOPAGE = (__force vm_fault_t)0x000100,
	VM_FAULT_LOCKED = (__force vm_fault_t)0x000200,
	VM_FAULT_RETRY = (__force vm_fault_t)0x000400,
	VM_FAULT_FALLBACK = (__force vm_fault_t)0x000800,
};

#define VM_FAULT_ERROR                                       \
	(VM_FAULT_OOM | VM_FAULT_SIGBUS | VM_FAULT_SIGSEGV | \
	 VM_FAULT_HWPOISON | VM_FAULT_HWPOISON_LARGE | VM_FAULT_FALLBACK)

/**
 * enum fault_flag - Fault flag definitions.
 * @FAULT_FLAG_WRITE: Fault was a write fault.
 * @FAULT_FLAG_MKWRITE: Fault was mkwrite of existing PTE.
 * @FAULT_FLAG_ALLOW_RETRY: Allow to retry the fault if blocked.
 * @FAULT_FLAG_RETRY_NOWAIT: Don't drop mmap_lock and wait when retrying.
 * @FAULT_FLAG_KILLABLE: The fault task is in SIGKILL killable region.
 * @FAULT_FLAG_TRIED: The fault has been tried once.
 * @FAULT_FLAG_USER: The fault originated in userspace.
 * @FAULT_FLAG_REMOTE: The fault is not for current task/mm.
 * @FAULT_FLAG_INSTRUCTION: The fault was during an instruction fetch.
 * @FAULT_FLAG_INTERRUPTIBLE: The fault can be interrupted by non-fatal signals.
 * @FAULT_FLAG_UNSHARE: The fault is an unsharing request to unshare (and mark
 *                      exclusive) a possibly shared anonymous page that is
 *                      mapped R/O.
 * @FAULT_FLAG_ORIG_PTE_VALID: whether the fault has vmf->orig_pte cached.
 *                        We should only access orig_pte if this flag set.
 *
 * About @FAULT_FLAG_ALLOW_RETRY and @FAULT_FLAG_TRIED: we can specify
 * whether we would allow page faults to retry by specifying these two
 * fault flags correctly.  Currently there can be three legal combinations:
 *
 * (a) ALLOW_RETRY and !TRIED:  this means the page fault allows retry, and
 *                              this is the first try
 *
 * (b) ALLOW_RETRY and TRIED:   this means the page fault allows retry, and
 *                              we've already tried at least once
 *
 * (c) !ALLOW_RETRY and !TRIED: this means the page fault does not allow retry
 *
 * The unlisted combination (!ALLOW_RETRY && TRIED) is illegal and should never
 * be used.  Note that page faults can be allowed to retry for multiple times,
 * in which case we'll have an initial fault with flags (a) then later on
 * continuous faults with flags (b).  We should always try to detect pending
 * signals before a retry to make sure the continuous page faults can still be
 * interrupted if necessary.
 *
 * The combination FAULT_FLAG_WRITE|FAULT_FLAG_UNSHARE is illegal.
 * FAULT_FLAG_UNSHARE is ignored and treated like an ordinary read fault when
 * no existing R/O-mapped anonymous page is encountered.
 */
enum fault_flag {
	FAULT_FLAG_WRITE = 1 << 0,
	FAULT_FLAG_ALLOW_RETRY = 1 << 2,
	FAULT_FLAG_TRIED = 1 << 5,
};

#endif /* _LINUX_MM_TYPES_H */
