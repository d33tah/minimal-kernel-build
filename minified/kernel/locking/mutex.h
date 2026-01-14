 
 

 
struct mutex_waiter {
	struct list_head	list;
	struct task_struct	*task;
	struct ww_acquire_ctx	*ww_ctx;
};
/* debug_mutex_* macros removed - all were empty stubs */
