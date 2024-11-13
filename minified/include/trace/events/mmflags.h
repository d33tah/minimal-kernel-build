/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/node.h>
#include <linux/mmzone.h>
#include <linux/compaction.h>
/*
 * The order of these masks is important. Matching masks will be seen
 * first and the left over flags will end up showing by themselves.
 *
 * For example, if we have GFP_KERNEL before GFP_USER we wil get:
 *
 *  GFP_KERNEL|GFP_HARDWALL
 *
 * Thus most bits set go first.
 */

#define gfpflag_string(flag) {(__force unsigned long)flag, #flag}

#define __def_gfpflag_names			\
	gfpflag_string(GFP_TRANSHUGE),		\
	gfpflag_string(GFP_TRANSHUGE_LIGHT),	\
	gfpflag_string(GFP_HIGHUSER_MOVABLE),	\
	gfpflag_string(GFP_HIGHUSER),		\
	gfpflag_string(GFP_USER),		\
	gfpflag_string(GFP_KERNEL_ACCOUNT),	\
	gfpflag_string(GFP_KERNEL),		\
	gfpflag_string(GFP_NOFS),		\
	gfpflag_string(GFP_ATOMIC),		\
	gfpflag_string(GFP_NOIO),		\
	gfpflag_string(GFP_NOWAIT),		\
	gfpflag_string(GFP_DMA),		\
	gfpflag_string(__GFP_HIGHMEM),		\
	gfpflag_string(GFP_DMA32),		\
	gfpflag_string(__GFP_HIGH),		\
	gfpflag_string(__GFP_ATOMIC),		\
	gfpflag_string(__GFP_IO),		\
	gfpflag_string(__GFP_FS),		\
	gfpflag_string(__GFP_NOWARN),		\
	gfpflag_string(__GFP_RETRY_MAYFAIL),	\
	gfpflag_string(__GFP_NOFAIL),		\
	gfpflag_string(__GFP_NORETRY),		\
	gfpflag_string(__GFP_COMP),		\
	gfpflag_string(__GFP_ZERO),		\
	gfpflag_string(__GFP_NOMEMALLOC),	\
	gfpflag_string(__GFP_MEMALLOC),		\
	gfpflag_string(__GFP_HARDWALL),		\
	gfpflag_string(__GFP_THISNODE),		\
	gfpflag_string(__GFP_RECLAIMABLE),	\
	gfpflag_string(__GFP_MOVABLE),		\
	gfpflag_string(__GFP_ACCOUNT),		\
	gfpflag_string(__GFP_WRITE),		\
	gfpflag_string(__GFP_RECLAIM),		\
	gfpflag_string(__GFP_DIRECT_RECLAIM),	\
	gfpflag_string(__GFP_KSWAPD_RECLAIM),	\
	gfpflag_string(__GFP_ZEROTAGS)

#define __def_gfpflag_names_kasan

#define show_gfp_flags(flags)						\
	(flags) ? __print_flags(flags, "|",				\
	__def_gfpflag_names __def_gfpflag_names_kasan			\
	) : "none"

#define IF_HAVE_PG_MLOCK(flag,string) ,{1UL << flag, string}

#define IF_HAVE_PG_UNCACHED(flag,string)

#define IF_HAVE_PG_HWPOISON(flag,string)

#define IF_HAVE_PG_IDLE(flag,string)

#define IF_HAVE_PG_ARCH_2(flag,string)

#define IF_HAVE_PG_SKIP_KASAN_POISON(flag,string)

#define __def_pageflag_names						\
	{1UL << PG_locked,		"locked"	},		\
	{1UL << PG_waiters,		"waiters"	},		\
	{1UL << PG_error,		"error"		},		\
	{1UL << PG_referenced,		"referenced"	},		\
	{1UL << PG_uptodate,		"uptodate"	},		\
	{1UL << PG_dirty,		"dirty"		},		\
	{1UL << PG_lru,			"lru"		},		\
	{1UL << PG_active,		"active"	},		\
	{1UL << PG_workingset,		"workingset"	},		\
	{1UL << PG_slab,		"slab"		},		\
	{1UL << PG_owner_priv_1,	"owner_priv_1"	},		\
	{1UL << PG_arch_1,		"arch_1"	},		\
	{1UL << PG_reserved,		"reserved"	},		\
	{1UL << PG_private,		"private"	},		\
	{1UL << PG_private_2,		"private_2"	},		\
	{1UL << PG_writeback,		"writeback"	},		\
	{1UL << PG_head,		"head"		},		\
	{1UL << PG_mappedtodisk,	"mappedtodisk"	},		\
	{1UL << PG_reclaim,		"reclaim"	},		\
	{1UL << PG_swapbacked,		"swapbacked"	},		\
	{1UL << PG_unevictable,		"unevictable"	}		\
IF_HAVE_PG_MLOCK(PG_mlocked,		"mlocked"	)		\
IF_HAVE_PG_UNCACHED(PG_uncached,	"uncached"	)		\
IF_HAVE_PG_HWPOISON(PG_hwpoison,	"hwpoison"	)		\
IF_HAVE_PG_IDLE(PG_young,		"young"		)		\
IF_HAVE_PG_IDLE(PG_idle,		"idle"		)		\
IF_HAVE_PG_ARCH_2(PG_arch_2,		"arch_2"	)		\
IF_HAVE_PG_SKIP_KASAN_POISON(PG_skip_kasan_poison, "skip_kasan_poison")

#define show_page_flags(flags)						\
	(flags) ? __print_flags(flags, "|",				\
	__def_pageflag_names						\
	) : "none"

#define __VM_ARCH_SPECIFIC_1 {VM_PAT,     "pat"           }

#define IF_HAVE_VM_SOFTDIRTY(flag,name)

# define IF_HAVE_UFFD_MINOR(flag, name)

#define __def_vmaflag_names						\
	{VM_READ,			"read"		},		\
	{VM_WRITE,			"write"		},		\
	{VM_EXEC,			"exec"		},		\
	{VM_SHARED,			"shared"	},		\
	{VM_MAYREAD,			"mayread"	},		\
	{VM_MAYWRITE,			"maywrite"	},		\
	{VM_MAYEXEC,			"mayexec"	},		\
	{VM_MAYSHARE,			"mayshare"	},		\
	{VM_GROWSDOWN,			"growsdown"	},		\
	{VM_UFFD_MISSING,		"uffd_missing"	},		\
IF_HAVE_UFFD_MINOR(VM_UFFD_MINOR,	"uffd_minor"	)		\
	{VM_PFNMAP,			"pfnmap"	},		\
	{VM_UFFD_WP,			"uffd_wp"	},		\
	{VM_LOCKED,			"locked"	},		\
	{VM_IO,				"io"		},		\
	{VM_SEQ_READ,			"seqread"	},		\
	{VM_RAND_READ,			"randread"	},		\
	{VM_DONTCOPY,			"dontcopy"	},		\
	{VM_DONTEXPAND,			"dontexpand"	},		\
	{VM_LOCKONFAULT,		"lockonfault"	},		\
	{VM_ACCOUNT,			"account"	},		\
	{VM_NORESERVE,			"noreserve"	},		\
	{VM_HUGETLB,			"hugetlb"	},		\
	{VM_SYNC,			"sync"		},		\
	__VM_ARCH_SPECIFIC_1				,		\
	{VM_WIPEONFORK,			"wipeonfork"	},		\
	{VM_DONTDUMP,			"dontdump"	},		\
IF_HAVE_VM_SOFTDIRTY(VM_SOFTDIRTY,	"softdirty"	)		\
	{VM_MIXEDMAP,			"mixedmap"	},		\
	{VM_HUGEPAGE,			"hugepage"	},		\
	{VM_NOHUGEPAGE,			"nohugepage"	},		\
	{VM_MERGEABLE,			"mergeable"	}		\

#define show_vma_flags(flags)						\
	(flags) ? __print_flags(flags, "|",				\
	__def_vmaflag_names						\
	) : "none"

#define COMPACTION_STATUS
#define COMPACTION_PRIORITY
#define COMPACTION_FEEDBACK

#define IFDEF_ZONE_DMA(X)

#define IFDEF_ZONE_DMA32(X)

#define IFDEF_ZONE_HIGHMEM(X)

#define ZONE_TYPE						\
	IFDEF_ZONE_DMA(		EM (ZONE_DMA,	 "DMA"))	\
	IFDEF_ZONE_DMA32(	EM (ZONE_DMA32,	 "DMA32"))	\
				EM (ZONE_NORMAL, "Normal")	\
	IFDEF_ZONE_HIGHMEM(	EM (ZONE_HIGHMEM,"HighMem"))	\
				EMe(ZONE_MOVABLE,"Movable")

#define LRU_NAMES		\
		EM (LRU_INACTIVE_ANON, "inactive_anon") \
		EM (LRU_ACTIVE_ANON, "active_anon") \
		EM (LRU_INACTIVE_FILE, "inactive_file") \
		EM (LRU_ACTIVE_FILE, "active_file") \
		EMe(LRU_UNEVICTABLE, "unevictable")

/*
 * First define the enums in the above macros to be exported to userspace
 * via TRACE_DEFINE_ENUM().
 */
#undef EM
#undef EMe
#define EM(a, b)	TRACE_DEFINE_ENUM(a);
#define EMe(a, b)	TRACE_DEFINE_ENUM(a);

COMPACTION_STATUS
COMPACTION_PRIORITY
/* COMPACTION_FEEDBACK are defines not enums. Not needed here. */
ZONE_TYPE
LRU_NAMES

/*
 * Now redefine the EM() and EMe() macros to map the enums to the strings
 * that will be printed in the output.
 */
#undef EM
#undef EMe
#define EM(a, b)	{a, b},
#define EMe(a, b)	{a, b}
