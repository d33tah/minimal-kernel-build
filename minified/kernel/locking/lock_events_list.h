 
 

#ifndef LOCK_EVENT
#define LOCK_EVENT(name)	LOCKEVENT_ ## name,
#endif


 
LOCK_EVENT(rwsem_sleep_reader)	 
LOCK_EVENT(rwsem_sleep_writer)	 
LOCK_EVENT(rwsem_wake_reader)	 
LOCK_EVENT(rwsem_wake_writer)	 
LOCK_EVENT(rwsem_opt_lock)	 
LOCK_EVENT(rwsem_opt_fail)	 
LOCK_EVENT(rwsem_opt_nospin)	 
LOCK_EVENT(rwsem_rlock)		 
LOCK_EVENT(rwsem_rlock_steal)	 
LOCK_EVENT(rwsem_rlock_fast)	 
LOCK_EVENT(rwsem_rlock_fail)	 
LOCK_EVENT(rwsem_rlock_handoff)	 
LOCK_EVENT(rwsem_wlock)		 
LOCK_EVENT(rwsem_wlock_fail)	 
LOCK_EVENT(rwsem_wlock_handoff)	 
