void complete(struct completion *x) { }
void __sched wait_for_completion(struct completion *x) { }
int __sched wait_for_completion_killable(struct completion *x) { return 0; }
