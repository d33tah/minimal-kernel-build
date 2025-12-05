
#ifndef __LINUX_WW_MUTEX_H
#define __LINUX_WW_MUTEX_H

#include <linux/mutex.h>
#include <linux/rtmutex.h>

#define WW_MUTEX_BASE			mutex
#define ww_mutex_base_init(l,n,k)	__mutex_init(l,n,k)
#define ww_mutex_base_is_locked(b)	mutex_is_locked((b))

struct ww_class {
	atomic_long_t stamp;
	struct lock_class_key acquire_key;
	struct lock_class_key mutex_key;
	const char *acquire_name;
	const char *mutex_name;
	unsigned int is_wait_die;
};

struct ww_mutex {
	struct WW_MUTEX_BASE base;
	struct ww_acquire_ctx *ctx;
};

struct ww_acquire_ctx {
	struct task_struct *task;
	unsigned long stamp;
	unsigned int acquired;
	unsigned short wounded;
	unsigned short is_wait_die;
};

#define __WW_CLASS_INITIALIZER(ww_class, _is_wait_die)	    \
		{ .stamp = ATOMIC_LONG_INIT(0) \
		, .acquire_name = #ww_class "_acquire" \
		, .mutex_name = #ww_class "_mutex" \
		, .is_wait_die = _is_wait_die }

#define DEFINE_WD_CLASS(classname) \
	struct ww_class classname = __WW_CLASS_INITIALIZER(classname, 1)

#define DEFINE_WW_CLASS(classname) \
	struct ww_class classname = __WW_CLASS_INITIALIZER(classname, 0)

static inline void ww_mutex_init(struct ww_mutex *lock,
				 struct ww_class *ww_class)
{
	ww_mutex_base_init(&lock->base, ww_class->mutex_name, &ww_class->mutex_key);
	lock->ctx = NULL;
}

static inline void ww_acquire_init(struct ww_acquire_ctx *ctx,
				   struct ww_class *ww_class)
{
	ctx->task = current;
	ctx->stamp = atomic_long_inc_return_relaxed(&ww_class->stamp);
	ctx->acquired = 0;
	ctx->wounded = false;
	ctx->is_wait_die = ww_class->is_wait_die;
}

static inline void ww_acquire_done(struct ww_acquire_ctx *ctx)
{
}

static inline void ww_acquire_fini(struct ww_acquire_ctx *ctx)
{
}

extern int   ww_mutex_lock(struct ww_mutex *lock, struct ww_acquire_ctx *ctx);

extern int __must_check ww_mutex_lock_interruptible(struct ww_mutex *lock,
						    struct ww_acquire_ctx *ctx);

static inline void
ww_mutex_lock_slow(struct ww_mutex *lock, struct ww_acquire_ctx *ctx)
{
	int ret;
	ret = ww_mutex_lock(lock, ctx);
	(void)ret;
}

static inline int __must_check
ww_mutex_lock_slow_interruptible(struct ww_mutex *lock,
				 struct ww_acquire_ctx *ctx)
{
	return ww_mutex_lock_interruptible(lock, ctx);
}

extern void ww_mutex_unlock(struct ww_mutex *lock);

extern int __must_check ww_mutex_trylock(struct ww_mutex *lock,
					 struct ww_acquire_ctx *ctx);

#endif
