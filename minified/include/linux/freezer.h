#ifndef FREEZER_H_INCLUDED
#define FREEZER_H_INCLUDED
struct task_struct;
static inline bool frozen(struct task_struct *p) { return false; }
#define freezable_schedule() schedule()
#endif
