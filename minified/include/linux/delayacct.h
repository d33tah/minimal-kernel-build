/* Minimal delayacct.h - stubs for !CONFIG_TASK_DELAY_ACCT */
#ifndef _LINUX_DELAYACCT_H
#define _LINUX_DELAYACCT_H

struct task_struct;

static inline void delayacct_init(void)
{}
static inline void delayacct_tsk_init(struct task_struct *tsk)
{}
static inline void delayacct_tsk_free(struct task_struct *tsk)
{}
static inline void delayacct_blkio_start(void)
{}
static inline void delayacct_blkio_end(struct task_struct *p)
{}
/* delayacct_add_tsk removed - unused */

#endif
