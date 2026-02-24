
void __init_waitqueue_head(struct wait_queue_head *wq_head, const char *name,
			   struct lock_class_key *key)
{
	spin_lock_init(&wq_head->lock);
	INIT_LIST_HEAD(&wq_head->head);
}

/* Simplified: no bookmark support needed for single-process kernel */
static void __wake_up_common(struct wait_queue_head *wq_head, unsigned int mode,
			     int nr_exclusive, void *key)
{
	wait_queue_entry_t *curr, *next;

	list_for_each_entry_safe(curr, next, &wq_head->head, entry) {
		int ret = curr->func(curr, mode, 0, key);
		if (ret < 0)
			break;
		if (ret && (curr->flags & WQ_FLAG_EXCLUSIVE) && !--nr_exclusive)
			break;
	}
}

void __wake_up(struct wait_queue_head *wq_head, unsigned int mode,
	       int nr_exclusive, void *key)
{
	unsigned long flags;

	spin_lock_irqsave(&wq_head->lock, flags);
	__wake_up_common(wq_head, mode, nr_exclusive, key);
	spin_unlock_irqrestore(&wq_head->lock, flags);
}

void __wake_up_locked_key_bookmark(struct wait_queue_head *wq_head,
				   unsigned int mode, void *key,
				   wait_queue_entry_t *bookmark)
{
	__wake_up_common(wq_head, mode, 1, key);
}
