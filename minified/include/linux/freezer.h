#ifndef FREEZER_H_INCLUDED
#define FREEZER_H_INCLUDED
struct task_struct;
static inline bool frozen(struct task_struct *p) { return false; }
static inline bool freezing(struct task_struct *p) { return false; }
static inline void freezer_do_not_count(void) {}
static inline void freezer_count(void) {}
#define freezable_schedule() schedule()
#endif 
