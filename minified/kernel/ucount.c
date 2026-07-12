
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/hash.h>
#include <linux/user_namespace.h>

struct ucounts init_ucounts = { .ns    = &init_user_ns, .uid   = GLOBAL_ROOT_UID, .count = ATOMIC_INIT(1), };

#define UCOUNTS_HASHTABLE_BITS 10
static struct hlist_head ucounts_hashtable[(1 << UCOUNTS_HASHTABLE_BITS)];
static DEFINE_SPINLOCK(ucounts_lock);

#define ucounts_hashfn(ns, uid)							hash_long((unsigned long)__kuid_val(uid) + (unsigned long)(ns), 		  UCOUNTS_HASHTABLE_BITS)
#define ucounts_hashentry(ns, uid)		(ucounts_hashtable + ucounts_hashfn(ns, uid))



static struct ucounts *find_ucounts(struct user_namespace *ns, kuid_t uid, struct hlist_head *hashent) {
	struct ucounts *ucounts;

	hlist_for_each_entry(ucounts, hashent, node) {
		if (uid_eq(ucounts->uid, uid) && (ucounts->ns == ns))
			return ucounts;
	}
	return NULL;
}

static void hlist_add_ucounts(struct ucounts *ucounts) {
	struct hlist_head *hashent = ucounts_hashentry(ucounts->ns, ucounts->uid);
	spin_lock_irq(&ucounts_lock);
	hlist_add_head(&ucounts->node, hashent);
	spin_unlock_irq(&ucounts_lock);
}

static inline bool get_ucounts_or_wrap(struct ucounts *ucounts) {
	 
	return !atomic_add_negative(1, &ucounts->count);
}

struct ucounts *get_ucounts(struct ucounts *ucounts) {
	if (!get_ucounts_or_wrap(ucounts)) {
		put_ucounts(ucounts);
		ucounts = NULL;
	}
	return ucounts;
}

struct ucounts *alloc_ucounts(struct user_namespace *ns, kuid_t uid) {
	struct hlist_head *hashent = ucounts_hashentry(ns, uid);
	struct ucounts *ucounts, *new;
	bool wrapped;

	spin_lock_irq(&ucounts_lock);
	ucounts = find_ucounts(ns, uid, hashent);
	if (!ucounts) {
		spin_unlock_irq(&ucounts_lock);

		new = kzalloc(sizeof(*new), GFP_KERNEL);
		if (!new)
			return NULL;

		new->ns = ns;
		new->uid = uid;
		atomic_set(&new->count, 1);

		spin_lock_irq(&ucounts_lock);
		ucounts = find_ucounts(ns, uid, hashent);
		if (ucounts) {
			kfree(new);
		} else {
			hlist_add_head(&new->node, hashent);
			get_user_ns(new->ns);
			spin_unlock_irq(&ucounts_lock);
			return new;
		}
	}
	wrapped = !get_ucounts_or_wrap(ucounts);
	spin_unlock_irq(&ucounts_lock);
	if (wrapped) {
		put_ucounts(ucounts);
		return NULL;
	}
	return ucounts;
}

void put_ucounts(struct ucounts *ucounts) {
	unsigned long flags;

	if (atomic_dec_and_lock_irqsave(&ucounts->count, &ucounts_lock, flags)) {
		hlist_del_init(&ucounts->node);
		spin_unlock_irqrestore(&ucounts_lock, flags);
		put_user_ns(ucounts->ns);
		kfree(ucounts);
	}
}

static inline bool atomic_long_inc_below(atomic_long_t *v, int u) {
	long c, old;
	c = atomic_long_read(v);
	for (;;) {
		if (unlikely(c >= u))
			return false;
		old = atomic_long_cmpxchg(v, c, c+1);
		if (likely(old == c))
			return true;
		c = old;
	}
}

struct ucounts *inc_ucount(struct user_namespace *ns, kuid_t uid, enum ucount_type type) {
	struct ucounts *ucounts, *iter, *bad;
	struct user_namespace *tns;
	ucounts = alloc_ucounts(ns, uid);
	for (iter = ucounts; iter; iter = tns->ucounts) {
		long max;
		tns = iter->ns;
		max = READ_ONCE(tns->ucount_max[type]);
		if (!atomic_long_inc_below(&iter->ucount[type], max))
			goto fail;
	}
	return ucounts;
fail:
	bad = iter;
	for (iter = ucounts; iter != bad; iter = iter->ns->ucounts)
		atomic_long_dec(&iter->ucount[type]);

	put_ucounts(ucounts);
	return NULL;
}

void dec_ucount(struct ucounts *ucounts, enum ucount_type type) {
	struct ucounts *iter;
	for (iter = ucounts; iter; iter = iter->ns->ucounts) {
		long dec = atomic_long_dec_if_positive(&iter->ucount[type]);
		WARN_ON_ONCE(dec < 0);
	}
	put_ucounts(ucounts);
}

long inc_rlimit_ucounts(struct ucounts *ucounts, enum ucount_type type, long v) {
	struct ucounts *iter;
	long max = LONG_MAX;
	long ret = 0;

	for (iter = ucounts; iter; iter = iter->ns->ucounts) {
		long new = atomic_long_add_return(v, &iter->ucount[type]);
		if (new < 0 || new > max)
			ret = LONG_MAX;
		else if (iter == ucounts)
			ret = new;
		max = READ_ONCE(iter->ns->ucount_max[type]);
	}
	return ret;
}

bool dec_rlimit_ucounts(struct ucounts *ucounts, enum ucount_type type, long v) {
	/*
	 * RUNTIME-DEAD ANCHOR-STUB: both call sites are runtime-dead and both
	 * ignore the return value. commit_creds() only calls this behind its
	 * user/user_ns-change conditional, never taken on a 1-shot boot; and
	 * copy_process()'s bad_fork_cleanup_count rollback never runs because
	 * copy_process always succeeds at boot (HIT=False). The counter
	 * decrement is unobservable when its matching increment never ran.
	 */
	return false;
}

static __init int user_namespace_sysctl_init(void) {
	hlist_add_ucounts(&init_ucounts);
	inc_rlimit_ucounts(&init_ucounts, UCOUNT_RLIMIT_NPROC, 1);
	return 0;
}
subsys_initcall(user_namespace_sysctl_init);
