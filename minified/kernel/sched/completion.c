void complete(struct completion *x) { }
/* complete_all removed - not called */
void __sched wait_for_completion(struct completion *x) { }
unsigned long __sched wait_for_completion_timeout(struct completion *x, unsigned long timeout) { return timeout; }
int __sched wait_for_completion_killable(struct completion *x) { return 0; }
