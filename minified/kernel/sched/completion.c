void complete(struct completion *x) { }
/* complete_all removed - not called */
void __sched wait_for_completion(struct completion *x) { }
/* wait_for_completion_timeout removed - not called */
int __sched wait_for_completion_killable(struct completion *x) { return 0; }
