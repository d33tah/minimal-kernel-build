/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SEM_H
#define _LINUX_SEM_H



struct task_struct;
struct sem_undo_list;


struct sysv_sem {
	/* empty */
};

static inline int copy_semundo(unsigned long clone_flags, struct task_struct *tsk)
{
	return 0;
}

static inline void exit_sem(struct task_struct *tsk)
{
	return;
}

#endif /* _LINUX_SEM_H */
