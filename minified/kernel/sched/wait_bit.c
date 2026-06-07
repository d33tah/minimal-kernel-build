
#define WAIT_TABLE_BITS 4 /* Reduced from 8 for minimal boot */
#define WAIT_TABLE_SIZE (1 << WAIT_TABLE_BITS)

static wait_queue_head_t bit_wait_table[WAIT_TABLE_SIZE] __cacheline_aligned;

void wake_up_bit(void *word, int bit)
{
	const int shift = BITS_PER_LONG == 32 ? 5 : 6;
	unsigned long val = (unsigned long)word << shift | bit;
	struct wait_queue_head *wq_head =
		bit_wait_table + hash_long(val, WAIT_TABLE_BITS);
	struct wait_bit_key key = __WAIT_BIT_KEY_INITIALIZER(word, bit);

	if (waitqueue_active(wq_head))
		__wake_up(wq_head, TASK_NORMAL, 1, &key);
}

void __init wait_bit_init(void)
{
	int i;

	for (i = 0; i < WAIT_TABLE_SIZE; i++)
		init_waitqueue_head(bit_wait_table + i);
}
