
#ifndef _LINUX_MODULE_H
#define _LINUX_MODULE_H

#include <linux/list.h>
#include <linux/stat.h>
#include <linux/buildid.h>
#include <linux/compiler.h>
#include <linux/cache.h>
#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/elf.h>
#include <linux/stringify.h>
#include <linux/kobject.h>
#include <linux/moduleparam.h>
#include <linux/jump_label.h>
#include <linux/export.h>

/* Inlined from rbtree_latch.h */
#include <linux/rbtree.h>
#include <linux/seqlock.h>
#include <linux/rcupdate.h>

struct latch_tree_node {
	struct rb_node node[2];
};

struct latch_tree_root {
	seqcount_latch_t	seq;
	struct rb_root		tree[2];
};

struct latch_tree_ops {
	bool (*less)(struct latch_tree_node *a, struct latch_tree_node *b);
	int  (*comp)(void *key,                 struct latch_tree_node *b);
};

static __always_inline struct latch_tree_node *
__lt_from_rb(struct rb_node *node, int idx)
{
	return container_of(node, struct latch_tree_node, node[idx]);
}

static __always_inline void
__lt_insert(struct latch_tree_node *ltn, struct latch_tree_root *ltr, int idx,
	    bool (*less)(struct latch_tree_node *a, struct latch_tree_node *b))
{
	struct rb_root *root = &ltr->tree[idx];
	struct rb_node **link = &root->rb_node;
	struct rb_node *node = &ltn->node[idx];
	struct rb_node *parent = NULL;
	struct latch_tree_node *ltp;

	while (*link) {
		parent = *link;
		ltp = __lt_from_rb(parent, idx);

		if (less(ltn, ltp))
			link = &parent->rb_left;
		else
			link = &parent->rb_right;
	}

	rb_link_node_rcu(node, parent, link);
	rb_insert_color(node, root);
}

static __always_inline void
__lt_erase(struct latch_tree_node *ltn, struct latch_tree_root *ltr, int idx)
{
	rb_erase(&ltn->node[idx], &ltr->tree[idx]);
}

static __always_inline struct latch_tree_node *
__lt_find(void *key, struct latch_tree_root *ltr, int idx,
	  int (*comp)(void *key, struct latch_tree_node *node))
{
	struct rb_node *node = rcu_dereference_raw(ltr->tree[idx].rb_node);
	struct latch_tree_node *ltn;
	int c;

	while (node) {
		ltn = __lt_from_rb(node, idx);
		c = comp(key, ltn);

		if (c < 0)
			node = rcu_dereference_raw(node->rb_left);
		else if (c > 0)
			node = rcu_dereference_raw(node->rb_right);
		else
			return ltn;
	}

	return NULL;
}

static __always_inline void
latch_tree_insert(struct latch_tree_node *node,
		  struct latch_tree_root *root,
		  const struct latch_tree_ops *ops)
{
	raw_write_seqcount_latch(&root->seq);
	__lt_insert(node, root, 0, ops->less);
	raw_write_seqcount_latch(&root->seq);
	__lt_insert(node, root, 1, ops->less);
}

static __always_inline void
latch_tree_erase(struct latch_tree_node *node,
		 struct latch_tree_root *root,
		 const struct latch_tree_ops *ops)
{
	raw_write_seqcount_latch(&root->seq);
	__lt_erase(node, root, 0);
	raw_write_seqcount_latch(&root->seq);
	__lt_erase(node, root, 1);
}

static __always_inline struct latch_tree_node *
latch_tree_find(void *key, struct latch_tree_root *root,
		const struct latch_tree_ops *ops)
{
	struct latch_tree_node *node;
	unsigned int seq;

	do {
		seq = raw_read_seqcount_latch(&root->seq);
		node = __lt_find(key, root, seq & 1, ops->comp);
	} while (read_seqcount_latch_retry(&root->seq, seq));

	return node;
}
/* End of inlined rbtree_latch.h content */

#include <linux/error-injection.h>
#include <linux/tracepoint-defs.h>
#include <linux/srcu.h>
#include <linux/static_call_types.h>


#include <linux/percpu.h>
#include <asm/module.h>

#define MODULE_NAME_LEN MAX_PARAM_PREFIX_LEN

struct modversion_info {
	unsigned long crc;
	char name[MODULE_NAME_LEN];
};

struct module;
struct exception_table_entry;

struct module_kobject {
	struct kobject kobj;
	struct module *mod;
	struct kobject *drivers_dir;
	struct module_param_attrs *mp;
	struct completion *kobj_completion;
} __randomize_layout;

struct module_attribute {
	struct attribute attr;
	ssize_t (*show)(struct module_attribute *, struct module_kobject *,
			char *);
	ssize_t (*store)(struct module_attribute *, struct module_kobject *,
			 const char *, size_t count);
	void (*setup)(struct module *, const char *);
	int (*test)(struct module *);
	void (*free)(struct module *);
};

struct module_version_attribute;

extern int init_module(void);
extern void cleanup_module(void);

#ifndef MODULE
#define module_init(x)	__initcall(x);

#define module_exit(x)	__exitcall(x);

#else  

#define early_initcall(fn)		module_init(fn)
#define core_initcall(fn)		module_init(fn)
#define core_initcall_sync(fn)		module_init(fn)
#define postcore_initcall(fn)		module_init(fn)
#define postcore_initcall_sync(fn)	module_init(fn)
#define arch_initcall(fn)		module_init(fn)
#define subsys_initcall(fn)		module_init(fn)
#define subsys_initcall_sync(fn)	module_init(fn)
#define fs_initcall(fn)			module_init(fn)
#define fs_initcall_sync(fn)		module_init(fn)
#define rootfs_initcall(fn)		module_init(fn)
#define device_initcall(fn)		module_init(fn)
#define device_initcall_sync(fn)	module_init(fn)
#define late_initcall(fn)		module_init(fn)
#define late_initcall_sync(fn)		module_init(fn)

#define console_initcall(fn)		module_init(fn)

#define module_init(initfn)					\
	static inline initcall_t __maybe_unused __inittest(void)		\
	{ return initfn; }					\
	int init_module(void) __copy(initfn)			\
		__attribute__((alias(#initfn)));		\
	__CFI_ADDRESSABLE(init_module, __initdata);

#define module_exit(exitfn)					\
	static inline exitcall_t __maybe_unused __exittest(void)		\
	{ return exitfn; }					\
	void cleanup_module(void) __copy(exitfn)		\
		__attribute__((alias(#exitfn)));		\
	__CFI_ADDRESSABLE(cleanup_module, __exitdata);

#endif

#define __init_or_module __init
#define __initdata_or_module __initdata
#define __initconst_or_module __initconst
#define __INIT_OR_MODULE __INIT
#define __INITDATA_OR_MODULE __INITDATA
#define __INITRODATA_OR_MODULE __INITRODATA

#define MODULE_INFO(tag, info) __MODULE_INFO(tag, tag, info)

#define MODULE_ALIAS(_alias) MODULE_INFO(alias, _alias)

#define MODULE_SOFTDEP(_softdep) MODULE_INFO(softdep, _softdep)

#ifdef MODULE
#define MODULE_FILE
#else
#define MODULE_FILE	MODULE_INFO(file, KBUILD_MODFILE);
#endif

#define MODULE_LICENSE(_license) MODULE_FILE MODULE_INFO(license, _license)

#define MODULE_AUTHOR(_author) MODULE_INFO(author, _author)

#define MODULE_DESCRIPTION(_description) MODULE_INFO(description, _description)

#ifdef MODULE
#define MODULE_DEVICE_TABLE(type, name)					\
extern typeof(name) __mod_##type##__##name##_device_table		\
  __attribute__ ((unused, alias(__stringify(name))))
#else   
#define MODULE_DEVICE_TABLE(type, name)
#endif


#define MODULE_VERSION(_version) MODULE_INFO(version, _version)

#define MODULE_FIRMWARE(_firmware) MODULE_INFO(firmware, _firmware)

#define MODULE_IMPORT_NS(ns)	MODULE_INFO(import_ns, __stringify(ns))

struct notifier_block;


/* __module_address, __module_text_address, is_module_address,
   is_module_percpu_address removed - unused (only is_module_text_address kept) */

static inline bool is_module_text_address(unsigned long addr)
{
	return false;
}

#define symbol_get(x) ({ extern typeof(x) x __attribute__((weak,visibility("hidden"))); &(x); })
#define symbol_put(x) do { } while (0)
/* symbol_put_addr removed - unused */

static inline void __module_get(struct module *module)
{
}

static inline bool try_module_get(struct module *module)
{
	return true;
}

static inline void module_put(struct module *module)
{
}

#define module_name(mod) "kernel"

#define module_put_and_kthread_exit(code) kthread_exit(code)

/* symbol_request, module_kallsyms_on_each_symbol removed - unused */

#define __MODULE_STRING(x) __stringify(x)

#endif  
