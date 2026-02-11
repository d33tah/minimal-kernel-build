void __init_swait_queue_head(struct swait_queue_head *q, const char *name,
			     struct lock_class_key *key)
{
	raw_spin_lock_init(&q->lock);
	INIT_LIST_HEAD(&q->task_list);
}

/* swake_up_locked, swake_up_one removed - never called (~18 LOC) */
/* __prepare_to_swait, prepare_to_swait_event, finish_swait removed - never called */
