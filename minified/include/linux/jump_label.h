#ifndef _LINUX_JUMP_LABEL_H
#define _LINUX_JUMP_LABEL_H


#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <linux/compiler.h>

extern bool static_key_initialized;

#define STATIC_KEY_CHECK_USE(key) WARN(!static_key_initialized,		      \
				    "%s(): static key '%pS' used before call to jump_label_init()", \
				    __func__, (key))

struct static_key {
	atomic_t enabled;
};

#endif  


#ifndef __ASSEMBLY__

/* enum jump_label_type removed - never used */

struct module;


#include <linux/atomic.h>
#include <linux/bug.h>

static __always_inline int static_key_count(struct static_key *key)
{
	return arch_atomic_read(&key->enabled);
}

static __always_inline void jump_label_init(void)
{
	static_key_initialized = true;
}

static inline void static_key_slow_inc(struct static_key *key)
{
	STATIC_KEY_CHECK_USE(key);
	atomic_inc(&key->enabled);
}

static inline void static_key_slow_dec(struct static_key *key)
{
	STATIC_KEY_CHECK_USE(key);
	atomic_dec(&key->enabled);
}

/* static_key_slow_inc_cpuslocked, static_key_slow_dec_cpuslocked removed - never used */


static inline void static_key_enable(struct static_key *key)
{
	STATIC_KEY_CHECK_USE(key);

	if (atomic_read(&key->enabled) != 0) {
		WARN_ON_ONCE(atomic_read(&key->enabled) != 1);
		return;
	}
	atomic_set(&key->enabled, 1);
}

static inline void static_key_disable(struct static_key *key)
{
	STATIC_KEY_CHECK_USE(key);

	if (atomic_read(&key->enabled) != 1) {
		WARN_ON_ONCE(atomic_read(&key->enabled) != 0);
		return;
	}
	atomic_set(&key->enabled, 0);
}

/* static_key_enable_cpuslocked, static_key_disable_cpuslocked removed - never used */

#define STATIC_KEY_INIT_TRUE	{ .enabled = ATOMIC_INIT(1) }
#define STATIC_KEY_INIT_FALSE	{ .enabled = ATOMIC_INIT(0) }
/* STATIC_KEY_INIT removed - never used */



struct static_key_true {
	struct static_key key;
};

struct static_key_false {
	struct static_key key;
};

#define STATIC_KEY_TRUE_INIT  (struct static_key_true) { .key = STATIC_KEY_INIT_TRUE,  }
#define STATIC_KEY_FALSE_INIT (struct static_key_false){ .key = STATIC_KEY_INIT_FALSE, }

#define DEFINE_STATIC_KEY_TRUE(name)	\
	struct static_key_true name = STATIC_KEY_TRUE_INIT

#define DECLARE_STATIC_KEY_TRUE(name)	\
	extern struct static_key_true name

#define DEFINE_STATIC_KEY_FALSE(name)	\
	struct static_key_false name = STATIC_KEY_FALSE_INIT

#define DEFINE_STATIC_KEY_FALSE_RO(name)	\
	struct static_key_false name __ro_after_init = STATIC_KEY_FALSE_INIT

#define DECLARE_STATIC_KEY_FALSE(name)	\
	extern struct static_key_false name


#define _DEFINE_STATIC_KEY_1(name)	DEFINE_STATIC_KEY_TRUE(name)
#define _DEFINE_STATIC_KEY_0(name)	DEFINE_STATIC_KEY_FALSE(name)
#define DEFINE_STATIC_KEY_MAYBE(cfg, name)			\
	__PASTE(_DEFINE_STATIC_KEY_, IS_ENABLED(cfg))(name)

/* DEFINE_STATIC_KEY_MAYBE_RO, _DEFINE_STATIC_KEY_RO_* removed - never used */

#define _DECLARE_STATIC_KEY_1(name)	DECLARE_STATIC_KEY_TRUE(name)
#define _DECLARE_STATIC_KEY_0(name)	DECLARE_STATIC_KEY_FALSE(name)
#define DECLARE_STATIC_KEY_MAYBE(cfg, name)			\
	__PASTE(_DECLARE_STATIC_KEY_, IS_ENABLED(cfg))(name)

extern bool ____wrong_branch_error(void);

#define static_key_enabled(x)							\
({										\
	if (!__builtin_types_compatible_p(typeof(*x), struct static_key) &&	\
	    !__builtin_types_compatible_p(typeof(*x), struct static_key_true) &&\
	    !__builtin_types_compatible_p(typeof(*x), struct static_key_false))	\
		____wrong_branch_error();					\
	static_key_count((struct static_key *)x) > 0;				\
})


#define static_branch_likely(x)		likely_notrace(static_key_enabled(&(x)->key))
#define static_branch_unlikely(x)	unlikely_notrace(static_key_enabled(&(x)->key))


#define static_branch_maybe(config, x)					\
	(IS_ENABLED(config) ? static_branch_likely(x)			\
			    : static_branch_unlikely(x))


#define static_branch_inc(x)		static_key_slow_inc(&(x)->key)
#define static_branch_dec(x)		static_key_slow_dec(&(x)->key)
/* static_branch_inc_cpuslocked, static_branch_dec_cpuslocked removed - never used */


#define static_branch_enable(x)			static_key_enable(&(x)->key)
#define static_branch_disable(x)		static_key_disable(&(x)->key)
/* static_branch_enable_cpuslocked, static_branch_disable_cpuslocked removed - never used */

#endif  

#endif	 
