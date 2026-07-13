 
 
 



/*
 * _TIF_WORK_CTXSW_{NEXT,PREV} only covered TIF bits that are never set in this
 * build (NOCPUID/NOTSC/BLOCKSTEP tested-but-never-set; SSBD/SPEC_FORCE_UPDATE/
 * USER_RETURN_NOTIFY unreferenced) => the context-switch extra-work test was
 * always false and __switch_to_xtra was never called. Folded to a no-op.
 */
static inline void switch_to_extra(struct task_struct *prev, struct task_struct *next) { }
