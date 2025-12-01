
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
#include <linux/rbtree_latch.h>
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


static inline struct module *__module_address(unsigned long addr)
{
	return NULL;
}

static inline struct module *__module_text_address(unsigned long addr)
{
	return NULL;
}

static inline bool is_module_address(unsigned long addr)
{
	return false;
}

static inline bool is_module_percpu_address(unsigned long addr)
{
	return false;
}

static inline bool __is_module_percpu_address(unsigned long addr, unsigned long *can_addr)
{
	return false;
}

static inline bool is_module_text_address(unsigned long addr)
{
	return false;
}

static inline bool within_module_core(unsigned long addr,
				      const struct module *mod)
{
	return false;
}

static inline bool within_module_init(unsigned long addr,
				      const struct module *mod)
{
	return false;
}

static inline bool within_module(unsigned long addr, const struct module *mod)
{
	return false;
}

#define symbol_get(x) ({ extern typeof(x) x __attribute__((weak,visibility("hidden"))); &(x); })
#define symbol_put(x) do { } while (0)
#define symbol_put_addr(x) do { } while (0)

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

static inline const char *module_address_lookup(unsigned long addr,
					  unsigned long *symbolsize,
					  unsigned long *offset,
					  char **modname,
					  const unsigned char **modbuildid,
					  char *namebuf)
{
	return NULL;
}

static inline int lookup_module_symbol_name(unsigned long addr, char *symname)
{
	return -ERANGE;
}

static inline int lookup_module_symbol_attrs(unsigned long addr, unsigned long *size, unsigned long *offset, char *modname, char *name)
{
	return -ERANGE;
}

static inline int module_get_kallsym(unsigned int symnum, unsigned long *value,
					char *type, char *name,
					char *module_name, int *exported)
{
	return -ERANGE;
}

static inline unsigned long module_kallsyms_lookup_name(const char *name)
{
	return 0;
}

static inline int register_module_notifier(struct notifier_block *nb)
{
	 
	return 0;
}

static inline int unregister_module_notifier(struct notifier_block *nb)
{
	return 0;
}

#define module_put_and_kthread_exit(code) kthread_exit(code)

static inline void print_modules(void)
{
}

static inline bool module_requested_async_probing(struct module *module)
{
	return false;
}


static inline void set_module_sig_enforced(void)
{
}

static inline
void *dereference_module_function_descriptor(struct module *mod, void *ptr)
{
	return ptr;
}



#define symbol_request(x) try_then_request_module(symbol_get(x), "symbol:" #x)


#define __MODULE_STRING(x) __stringify(x)


static inline void module_bug_finalize(const Elf_Ehdr *hdr,
					const Elf_Shdr *sechdrs,
					struct module *mod)
{
}
static inline void module_bug_cleanup(struct module *mod) {}

static inline bool retpoline_module_ok(bool has_retpoline)
{
	return true;
}

static inline bool is_module_sig_enforced(void)
{
	return false;
}

static inline bool module_sig_ok(struct module *module)
{
	return true;
}

int module_kallsyms_on_each_symbol(int (*fn)(void *, const char *,
					     struct module *, unsigned long),
				   void *data);

#endif  
