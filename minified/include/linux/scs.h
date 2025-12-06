/* Minimal scs.h - stubs for !CONFIG_SHADOW_CALL_STACK */
#ifndef _LINUX_SCS_H
#define _LINUX_SCS_H

struct task_struct;

static inline void *scs_alloc(int node) { return NULL; }
static inline void scs_free(void *s) {}
static inline void scs_init(void) {}
static inline void scs_task_reset(struct task_struct *tsk) {}
static inline int scs_prepare(struct task_struct *tsk, int node) { return 0; }
static inline void scs_release(struct task_struct *tsk) {}
static inline bool task_scs_end_corrupted(struct task_struct *tsk) { return false; }


#endif  
