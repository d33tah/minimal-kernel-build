void complete(struct completion *x) { }
void complete_all(struct completion *x) { }
void __sched wait_for_completion(struct completion *x) { }
unsigned long __sched wait_for_completion_timeout(struct completion *x, unsigned long timeout) { return timeout; }
int __sched wait_for_completion_killable(struct completion *x) { return 0; }
