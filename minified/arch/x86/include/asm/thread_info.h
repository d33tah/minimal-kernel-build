/* SPDX-License-Identifier: GPL-2.0 */
/* thread_info.h: low-level thread information
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * - Incorporating suggestions made by Linus Torvalds and Dave Miller
 */

#ifndef _ASM_X86_THREAD_INFO_H
#define _ASM_X86_THREAD_INFO_H

#include <asm/page.h>

#define TOP_OF_KERNEL_STACK_PADDING 8

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct shares the supervisor stack pages
 */
#ifndef __ASSEMBLY__
struct task_struct;

struct thread_info {
	unsigned long flags; /* low level flags */
};

#define INIT_THREAD_INFO(tsk) \
	{                     \
		.flags = 0,   \
	}

#else /* !__ASSEMBLY__ */

#include <asm/asm-offsets.h>

#endif

#define TIF_NEED_RESCHED 3 /* rescheduling necessary */

#define _TIF_NEED_RESCHED (1 << TIF_NEED_RESCHED)

#ifndef __ASSEMBLY__

extern int arch_dup_task_struct(struct task_struct *dst,
				struct task_struct *src);
#endif

#endif /* _ASM_X86_THREAD_INFO_H */
