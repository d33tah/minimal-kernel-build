

/* Stubbed: complete not used */
void complete(struct completion *x) { }

/* Stubbed: complete_all not used */
void complete_all(struct completion *x) { }

/* Stubbed: wait_for_completion not used */
void __sched wait_for_completion(struct completion *x) { }

/* Stubbed: wait_for_completion_timeout not used */
unsigned long __sched wait_for_completion_timeout(struct completion *x, unsigned long timeout) { return timeout; }

/* Stubbed: wait_for_completion_interruptible not used */
int __sched wait_for_completion_interruptible(struct completion *x) { return 0; }

/* Stubbed: wait_for_completion_killable not used */
int __sched wait_for_completion_killable(struct completion *x) { return 0; }

/* Stubbed: completion_done not used */
bool completion_done(struct completion *x) { return true; }
