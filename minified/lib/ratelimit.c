

int ___ratelimit(struct ratelimit_state *rs, const char *func)
{
	/*
	 * RUNTIME-DEAD SAFE-FALLBACK STUB: ___ratelimit is the rate-limit core
	 * (returns 1 = "allowed to emit", 0 = "suppressed").  Its only callers
	 * are diagnostic/error paths -- irq debug print_irq_desc, the "No irq
	 * handler for vector" emerg, fs read_write warn, tty info -- none of
	 * which fire on a clean boot-once-and-print artifact (HIT=False in the
	 * exec trace).  Returning 1 is the behavior-preserving fallback: should
	 * any future call occur, the message is simply emitted unsuppressed
	 * (the suppression accounting is the only thing dropped).  The full
	 * jiffies/spinlock/printk_deferred body is therefore the dead payoff.
	 */
	return 1;
}
