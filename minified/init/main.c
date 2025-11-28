 
 

#define DEBUG		 

#include <linux/types.h>
#include <linux/extable.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/binfmts.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/stackprotector.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/memblock.h>
#include <linux/acpi.h>

#include <linux/bootconfig.h>
#include <linux/console.h>
#include <linux/nmi.h>
#include <linux/percpu.h>
#include <linux/start_kernel.h>
#include <linux/security.h>
#include <linux/smp.h>
#include <linux/profile.h>
#include <linux/kfence.h>
#include <linux/rcupdate.h>
#include <linux/srcu.h>
#include <linux/moduleparam.h>
#include <linux/kallsyms.h>
#include <linux/buildid.h>
#include <linux/writeback.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/cgroup.h>
#include <linux/efi.h>
#include <linux/tick.h>
#include <linux/sched/isolation.h>
#include <linux/interrupt.h>
#include <linux/taskstats_kern.h>
#include <linux/delayacct.h>
#include <linux/unistd.h>
#include <linux/utsname.h>
#include <linux/rmap.h>
#include <linux/mempolicy.h>
#include <linux/key.h>
#include <linux/page_ext.h>
#include <linux/debug_locks.h>
#include <linux/lockdep.h>
#include <linux/kmemleak.h>
#include <linux/padata.h>
#include <linux/pid_namespace.h>
#include <linux/device/driver.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched/init.h>
#include <linux/signal.h>
#include <linux/idr.h>
#include <linux/kgdb.h>
#include <linux/async.h>
#include <linux/shmem_fs.h>
#include <linux/slab.h>
#include <linux/perf_event.h>

#include <linux/ptrace.h>
#include <linux/blkdev.h>
#include <linux/sched/clock.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/context_tracking.h>
#include <linux/random.h>
#include <linux/list.h>
#include <linux/integrity.h>
#include <linux/proc_ns.h>
#include <linux/io.h>
#include <linux/cache.h>
#include <linux/jump_label.h>
#include <linux/mem_encrypt.h>
#include <linux/init_syscalls.h>
#include <linux/stackdepot.h>

#include <asm/io.h>
#include <asm/bugs.h>
#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/cacheflush.h>



static int kernel_init(void *);

extern void init_IRQ(void);
extern void radix_tree_init(void);

 
bool early_boot_irqs_disabled __read_mostly;

enum system_states system_state __read_mostly;

 
#define MAX_INIT_ARGS CONFIG_INIT_ENV_ARG_LIMIT
#define MAX_INIT_ENVS CONFIG_INIT_ENV_ARG_LIMIT

extern void time_init(void);
 
void (*__initdata late_time_init)(void);

 
char __initdata boot_command_line[COMMAND_LINE_SIZE];
 
char *saved_command_line;
 
static char *static_command_line;
 
static char *extra_command_line;
 
static char *extra_init_args;

# define bootconfig_found false
# define initargs_offs 0

static char *execute_command;
static char *ramdisk_execute_command = "/init";

 
bool static_key_initialized __read_mostly;

 
unsigned int reset_devices;

/* Stub: reset_devices cmdline not needed for minimal kernel */
static int __init set_reset_devices(char *str) { return 1; }
__setup("reset_devices", set_reset_devices);

static const char *argv_init[MAX_INIT_ARGS+2] = { "init", NULL, };
const char *envp_init[MAX_INIT_ENVS+2] = { "HOME=/", "TERM=linux", NULL, };
static const char *panic_later, *panic_param;

extern const struct obs_kernel_param __setup_start[], __setup_end[];

static bool __init obsolete_checksetup(char *line)
{
	const struct obs_kernel_param *p;
	bool had_early_param = false;

	p = __setup_start;
	do {
		int n = strlen(p->str);
		if (parameqn(line, p->str, n)) {
			if (p->early) {
				 
				if (line[n] == '\0' || line[n] == '=')
					had_early_param = true;
			} else if (!p->setup_func) {
				pr_warn("Parameter %s is obsolete, ignored\n",
					p->str);
				return true;
			} else if (p->setup_func(line + n))
				return true;
		}
		p++;
	} while (p < __setup_end);

	return had_early_param;
}

 
unsigned long loops_per_jiffy = (1<<12);

/* Stub: debug/quiet/loglevel cmdline not needed for minimal kernel */
static int __init debug_kernel(char *str) { return 0; }
static int __init quiet_kernel(char *str) { return 0; }
early_param("debug", debug_kernel);
early_param("quiet", quiet_kernel);

static int __init loglevel(char *str) { return 0; }
early_param("loglevel", loglevel);

/* Stub: bootconfig not needed for minimal kernel */
static void __init setup_boot_config(void) { }
static int __init warn_bootconfig(char *str) { return 0; }
#define exit_boot_config()	do {} while (0)
early_param("bootconfig", warn_bootconfig);

 
static void __init repair_env_string(char *param, char *val)
{
	if (val) {
		 
		if (val == param+strlen(param)+1)
			val[-1] = '=';
		else if (val == param+strlen(param)+2) {
			val[-2] = '=';
			memmove(val-1, val, strlen(val)+1);
		} else
			BUG();
	}
}

 
static int __init set_init_arg(char *param, char *val,
			       const char *unused, void *arg)
{
	unsigned int i;

	if (panic_later)
		return 0;

	repair_env_string(param, val);

	for (i = 0; argv_init[i]; i++) {
		if (i == MAX_INIT_ARGS) {
			panic_later = "init";
			panic_param = param;
			return 0;
		}
	}
	argv_init[i] = param;
	return 0;
}

 
static int __init unknown_bootoption(char *param, char *val,
				     const char *unused, void *arg)
{
	size_t len = strlen(param);

	repair_env_string(param, val);

	 
	if (obsolete_checksetup(param))
		return 0;

	 
	if (strnchr(param, len, '.'))
		return 0;

	if (panic_later)
		return 0;

	if (val) {
		 
		unsigned int i;
		for (i = 0; envp_init[i]; i++) {
			if (i == MAX_INIT_ENVS) {
				panic_later = "env";
				panic_param = param;
			}
			if (!strncmp(param, envp_init[i], len+1))
				break;
		}
		envp_init[i] = param;
	} else {
		 
		unsigned int i;
		for (i = 0; argv_init[i]; i++) {
			if (i == MAX_INIT_ARGS) {
				panic_later = "init";
				panic_param = param;
			}
		}
		argv_init[i] = param;
	}
	return 0;
}

static int __init init_setup(char *str)
{
	unsigned int i;

	execute_command = str;
	 
	for (i = 1; i < MAX_INIT_ARGS; i++)
		argv_init[i] = NULL;
	return 1;
}
__setup("init=", init_setup);

static int __init rdinit_setup(char *str)
{
	unsigned int i;

	ramdisk_execute_command = str;
	 
	for (i = 1; i < MAX_INIT_ARGS; i++)
		argv_init[i] = NULL;
	return 1;
}
__setup("rdinit=", rdinit_setup);

static const unsigned int setup_max_cpus = NR_CPUS;
static inline void setup_nr_cpu_ids(void) { }
static inline void smp_prepare_cpus(unsigned int maxcpus) { }

 
static void __init setup_command_line(char *command_line)
{
	size_t len, xlen = 0, ilen = 0;

	if (extra_command_line)
		xlen = strlen(extra_command_line);
	if (extra_init_args)
		ilen = strlen(extra_init_args) + 4;  

	len = xlen + strlen(boot_command_line) + 1;

	saved_command_line = memblock_alloc(len + ilen, SMP_CACHE_BYTES);
	if (!saved_command_line)
		panic("%s: Failed to allocate %zu bytes\n", __func__, len + ilen);

	static_command_line = memblock_alloc(len, SMP_CACHE_BYTES);
	if (!static_command_line)
		panic("%s: Failed to allocate %zu bytes\n", __func__, len);

	if (xlen) {
		 
		strcpy(saved_command_line, extra_command_line);
		strcpy(static_command_line, extra_command_line);
	}
	strcpy(saved_command_line + xlen, boot_command_line);
	strcpy(static_command_line + xlen, command_line);

	if (ilen) {
		 
		if (initargs_offs) {
			len = xlen + initargs_offs;
			strcpy(saved_command_line + len, extra_init_args);
			len += ilen - 4;	 
			strcpy(saved_command_line + len,
				boot_command_line + initargs_offs - 1);
		} else {
			len = strlen(saved_command_line);
			strcpy(saved_command_line + len, " -- ");
			len += 4;
			strcpy(saved_command_line + len, extra_init_args);
		}
	}
}

 

static __initdata DECLARE_COMPLETION(kthreadd_done);

noinline void __ref rest_init(void)
{
	struct task_struct *tsk;
	int pid;

	rcu_scheduler_starting();
	 
	pid = user_mode_thread(kernel_init, NULL, CLONE_FS);
	 
	rcu_read_lock();
	tsk = find_task_by_pid_ns(pid, &init_pid_ns);
	tsk->flags |= PF_NO_SETAFFINITY;
	set_cpus_allowed_ptr(tsk, cpumask_of(smp_processor_id()));
	rcu_read_unlock();

	numa_default_policy();
	pid = kernel_thread(kthreadd, NULL, CLONE_FS | CLONE_FILES);
	rcu_read_lock();
	kthreadd_task = find_task_by_pid_ns(pid, &init_pid_ns);
	rcu_read_unlock();

	 
	system_state = SYSTEM_SCHEDULING;

	complete(&kthreadd_done);

	 
	schedule_preempt_disabled();
	 
	cpu_startup_entry(CPUHP_ONLINE);
}

 
static int __init do_early_param(char *param, char *val,
				 const char *unused, void *arg)
{
	const struct obs_kernel_param *p;

	for (p = __setup_start; p < __setup_end; p++) {
		if ((p->early && parameq(param, p->str)) ||
		    (strcmp(param, "console") == 0 &&
		     strcmp(p->str, "earlycon") == 0)
		) {
			if (p->setup_func(val) != 0)
				pr_warn("Malformed early option '%s'\n", param);
		}
	}
	 
	return 0;
}

void __init parse_early_options(char *cmdline)
{
	parse_args("early options", cmdline, NULL, 0, 0, 0, NULL,
		   do_early_param);
}

 
void __init parse_early_param(void)
{
	static int done __initdata;
	static char tmp_cmdline[COMMAND_LINE_SIZE] __initdata;

	if (done)
		return;

	 
	strlcpy(tmp_cmdline, boot_command_line, COMMAND_LINE_SIZE);
	parse_early_options(tmp_cmdline);
	done = 1;
}

void __init __weak arch_post_acpi_subsys_init(void) { }

void __init __weak smp_setup_processor_id(void)
{
}

# if THREAD_SIZE >= PAGE_SIZE
void __init __weak thread_stack_cache_init(void)
{
}
#endif

void __init __weak mem_encrypt_init(void) { }

void __init __weak poking_init(void) { }

void __init __weak pgtable_cache_init(void) { }

void __init __weak trap_init(void) { }

bool initcall_debug;
core_param(initcall_debug, initcall_debug, bool, 0644);

#ifdef TRACEPOINTS_ENABLED
static void __init initcall_debug_enable(void);
#else
static inline void initcall_debug_enable(void)
{
}
#endif

 
static void __init report_meminit(void)
{
	/* Stub: mem auto-init reporting not needed for minimal kernel */
}

 
static void __init mm_init(void)
{
	 
	page_ext_init_flatmem();
	init_mem_debugging_and_hardening();
	kfence_alloc_pool();
	report_meminit();
	stack_depot_early_init();
	mem_init();
	mem_init_print_info();
	kmem_cache_init();
	 
	page_ext_init_flatmem_late();
	kmemleak_init();
	pgtable_init();
	vmalloc_init();
	 
	init_espfix_bsp();
}


void __init __weak arch_call_rest_init(void)
{
	rest_init();
}

static void __init print_unknown_bootoptions(void)
{
	/* Stub: boot option reporting not needed for minimal kernel */
}

asmlinkage __visible void __init __no_sanitize_address start_kernel(void)
{
	char *command_line;
	char *after_dashes;

	set_task_stack_end_magic(&init_task);
	smp_setup_processor_id();
	init_vmlinux_build_id();

	cgroup_init_early();

	local_irq_disable();
	early_boot_irqs_disabled = true;

	 
	boot_cpu_init();
	page_address_init();
	pr_notice("%s", linux_banner);
	early_security_init();
	setup_arch(&command_line);
	setup_boot_config();
	setup_command_line(command_line);
	setup_nr_cpu_ids();
	setup_per_cpu_areas();
	smp_prepare_boot_cpu();	 
	boot_cpu_hotplug_init();

	build_all_zonelists(NULL);
	page_alloc_init();

	pr_notice("Kernel command line: %s\n", saved_command_line);
	 
	jump_label_init();
	parse_early_param();
	after_dashes = parse_args("Booting kernel",
				  static_command_line, __start___param,
				  __stop___param - __start___param,
				  -1, -1, NULL, &unknown_bootoption);
	print_unknown_bootoptions();
	if (!IS_ERR_OR_NULL(after_dashes))
		parse_args("Setting init args", after_dashes, NULL, 0, -1, -1,
			   NULL, set_init_arg);
	if (extra_init_args)
		parse_args("Setting extra init args", extra_init_args,
			   NULL, 0, -1, -1, NULL, set_init_arg);

	 
	setup_log_buf(0);
	vfs_caches_init_early();
	sort_main_extable();
	trap_init();
	mm_init();

	 
	sched_init();

	if (WARN(!irqs_disabled(),
		 "Interrupts were enabled *very* early, fixing it\n"))
		local_irq_disable();
	radix_tree_init();

	 
	housekeeping_init();

	 
	workqueue_init_early();

	rcu_init();

	if (initcall_debug)
		initcall_debug_enable();

	context_tracking_init();
	 
	early_irq_init();
	init_IRQ();
	tick_init();
	rcu_init_nohz();
	init_timers();
	srcu_init();
	hrtimers_init();
	softirq_init();
	timekeeping_init();
	kfence_init();
	time_init();

	 
	random_init(command_line);
	boot_init_stack_canary();

	perf_event_init();
	profile_init();
	call_function_init();
	WARN(!irqs_disabled(), "Interrupts were enabled early\n");

	early_boot_irqs_disabled = false;
	local_irq_enable();

	kmem_cache_init_late();

	 
	console_init();
	if (panic_later)
		panic("Too many boot %s vars at `%s'", panic_later,
		      panic_param);

	lockdep_init();

	 
	locking_selftest();

	 
	mem_encrypt_init();

	if (initrd_start && !initrd_below_start_ok &&
	    page_to_pfn(virt_to_page((void *)initrd_start)) < min_low_pfn) {
		pr_crit("initrd overwritten (0x%08lx < 0x%08lx) - disabling it.\n",
		    page_to_pfn(virt_to_page((void *)initrd_start)),
		    min_low_pfn);
		initrd_start = 0;
	}
	setup_per_cpu_pageset();
	numa_policy_init();
	acpi_early_init();
	if (late_time_init)
		late_time_init();
	sched_clock_init();
	calibrate_delay();
	pid_idr_init();
	anon_vma_init();
	if (efi_enabled(EFI_RUNTIME_SERVICES))
		efi_enter_virtual_mode();
	thread_stack_cache_init();
	cred_init();
	fork_init();
	proc_caches_init();
	uts_ns_init();
	key_init();
	security_init();
	dbg_late_init();
	vfs_caches_init();
	pagecache_init();
	signals_init();
	seq_file_init();
	proc_root_init();
	nsfs_init();
	cpuset_init();
	cgroup_init();
	taskstats_init_early();
	delayacct_init();

	poking_init();
	check_bugs();

	acpi_subsystem_init();
	arch_post_acpi_subsys_init();

	 
	arch_call_rest_init();

	prevent_tail_call_optimization();
}

 
static void __init do_ctors(void)
{
 
}

/* Stub: initcall_blacklist not needed for minimal kernel */
static int __init initcall_blacklist(char *str) { return 0; }
static bool __init_or_module initcall_blacklisted(initcall_t fn) { return false; }
__setup("initcall_blacklist=", initcall_blacklist);

/* Stub: initcall debug tracing not needed for minimal kernel */
static __init_or_module void
trace_initcall_start_cb(void *data, initcall_t fn) { }

static __init_or_module void
trace_initcall_finish_cb(void *data, initcall_t fn, int ret) { }

static ktime_t initcall_calltime;

#ifdef TRACEPOINTS_ENABLED
static void __init initcall_debug_enable(void)
{
	int ret;

	ret = register_trace_initcall_start(trace_initcall_start_cb,
					    &initcall_calltime);
	ret |= register_trace_initcall_finish(trace_initcall_finish_cb,
					      &initcall_calltime);
	WARN(ret, "Failed to register initcall tracepoints\n");
}
# define do_trace_initcall_start	trace_initcall_start
# define do_trace_initcall_finish	trace_initcall_finish
#else
static inline void do_trace_initcall_start(initcall_t fn)
{
	if (!initcall_debug)
		return;
	trace_initcall_start_cb(&initcall_calltime, fn);
}
static inline void do_trace_initcall_finish(initcall_t fn, int ret)
{
	if (!initcall_debug)
		return;
	trace_initcall_finish_cb(&initcall_calltime, fn, ret);
}
#endif  

int __init_or_module do_one_initcall(initcall_t fn)
{
	int count = preempt_count();
	char msgbuf[64];
	int ret;

	if (initcall_blacklisted(fn))
		return -EPERM;

	do_trace_initcall_start(fn);
	ret = fn();
	do_trace_initcall_finish(fn, ret);

	msgbuf[0] = 0;

	if (preempt_count() != count) {
		sprintf(msgbuf, "preemption imbalance ");
		preempt_count_set(count);
	}
	if (irqs_disabled()) {
		strlcat(msgbuf, "disabled interrupts ", sizeof(msgbuf));
		local_irq_enable();
	}
	WARN(msgbuf[0], "initcall %pS returned with %s\n", fn, msgbuf);

	add_latent_entropy();
	return ret;
}


extern initcall_entry_t __initcall_start[];
extern initcall_entry_t __initcall0_start[];
extern initcall_entry_t __initcall1_start[];
extern initcall_entry_t __initcall2_start[];
extern initcall_entry_t __initcall3_start[];
extern initcall_entry_t __initcall4_start[];
extern initcall_entry_t __initcall5_start[];
extern initcall_entry_t __initcall6_start[];
extern initcall_entry_t __initcall7_start[];
extern initcall_entry_t __initcall_end[];

static initcall_entry_t *initcall_levels[] __initdata = {
	__initcall0_start,
	__initcall1_start,
	__initcall2_start,
	__initcall3_start,
	__initcall4_start,
	__initcall5_start,
	__initcall6_start,
	__initcall7_start,
	__initcall_end,
};

 
static const char *initcall_level_names[] __initdata = {
	"pure",
	"core",
	"postcore",
	"arch",
	"subsys",
	"fs",
	"device",
	"late",
};

static int __init ignore_unknown_bootoption(char *param, char *val,
			       const char *unused, void *arg)
{
	return 0;
}

static void __init do_initcall_level(int level, char *command_line)
{
	initcall_entry_t *fn;

	parse_args(initcall_level_names[level],
		   command_line, __start___param,
		   __stop___param - __start___param,
		   level, level,
		   NULL, ignore_unknown_bootoption);


	for (fn = initcall_levels[level]; fn < initcall_levels[level+1]; fn++)
		do_one_initcall(initcall_from_entry(fn));
}

static void __init do_initcalls(void)
{
	int level;
	size_t len = strlen(saved_command_line) + 1;
	char *command_line;

	command_line = kzalloc(len, GFP_KERNEL);
	if (!command_line)
		panic("%s: Failed to allocate %zu bytes\n", __func__, len);

	for (level = 0; level < ARRAY_SIZE(initcall_levels) - 1; level++) {
		 
		strcpy(command_line, saved_command_line);
		do_initcall_level(level, command_line);
	}

	kfree(command_line);
}

 
static void __init do_basic_setup(void)
{
	cpuset_init_smp();
	driver_init();
	init_irq_proc();
	do_ctors();
	do_initcalls();
}

static void __init do_pre_smp_initcalls(void)
{
	initcall_entry_t *fn;


	for (fn = __initcall_start; fn < __initcall0_start; fn++)
		do_one_initcall(initcall_from_entry(fn));
}

static int run_init_process(const char *init_filename)
{
	argv_init[0] = init_filename;
	pr_info("Run %s as init process\n", init_filename);
	return kernel_execve(init_filename, argv_init, envp_init);
}

static int try_to_run_init_process(const char *init_filename)
{
	int ret;

	ret = run_init_process(init_filename);

	if (ret && ret != -ENOENT) {
		pr_err("Starting init: %s exists but couldn't execute it (error %d)\n",
		       init_filename, ret);
	}

	return ret;
}

static noinline void __init kernel_init_freeable(void);

bool rodata_enabled __ro_after_init = true;
static int __init set_debug_rodata(char *str)
{
	if (strtobool(str, &rodata_enabled))
		pr_warn("Invalid option string for rodata: '%s'\n", str);
	return 1;
}
__setup("rodata=", set_debug_rodata);

static void mark_readonly(void)
{
	if (rodata_enabled) {
		 
		rcu_barrier();
		mark_rodata_ro();
	} else
		pr_info("Kernel memory protection disabled.\n");
}

void __weak free_initmem(void)
{
	free_initmem_default(POISON_FREE_INITMEM);
}

static int __ref kernel_init(void *unused)
{
	int ret;

	 
	wait_for_completion(&kthreadd_done);

	kernel_init_freeable();
	 
	async_synchronize_full();

	system_state = SYSTEM_FREEING_INITMEM;
	kprobe_free_init_mem();
	kgdb_free_init_mem();
	exit_boot_config();
	free_initmem();
	mark_readonly();

	system_state = SYSTEM_RUNNING;
	numa_default_policy();

	rcu_end_inkernel_boot();

	do_sysctl_args();

	if (ramdisk_execute_command) {
		ret = run_init_process(ramdisk_execute_command);
		if (!ret)
			return 0;
		pr_err("Failed to execute %s (error %d)\n",
		       ramdisk_execute_command, ret);
	}

	 
	if (execute_command) {
		ret = run_init_process(execute_command);
		if (!ret)
			return 0;
		panic("Requested init %s failed (error %d).",
		      execute_command, ret);
	}

	if (CONFIG_DEFAULT_INIT[0] != '\0') {
		ret = run_init_process(CONFIG_DEFAULT_INIT);
		if (ret)
			pr_err("Default init %s failed (error %d)\n",
			       CONFIG_DEFAULT_INIT, ret);
		else
			return 0;
	}

	if (!try_to_run_init_process("/sbin/init") ||
	    !try_to_run_init_process("/etc/init") ||
	    !try_to_run_init_process("/bin/init") ||
	    !try_to_run_init_process("/bin/sh"))
		return 0;

	panic("No working init found.  Try passing init= option to kernel. "
	      "See Linux Documentation/admin-guide/init.rst for guidance.");
}

 
void __init console_on_rootfs(void)
{
	struct file *file = filp_open("/dev/console", O_RDWR, 0);

	if (IS_ERR(file)) {
		pr_err("Warning: unable to open an initial console.\n");
		return;
	}
	init_dup(file);
	init_dup(file);
	init_dup(file);
	fput(file);
}

static noinline void __init kernel_init_freeable(void)
{
	 
	gfp_allowed_mask = __GFP_BITS_MASK;

	 
	set_mems_allowed(node_states[N_MEMORY]);

	cad_pid = get_pid(task_pid(current));

	smp_prepare_cpus(setup_max_cpus);

	workqueue_init();

	init_mm_internals();

	rcu_init_tasks_generic();
	do_pre_smp_initcalls();
	lockup_detector_init();

	smp_init();
	sched_init_smp();

	padata_init();
	page_alloc_init_late();
	 
	page_ext_init();

	do_basic_setup();

	wait_for_initramfs();
	console_on_rootfs();

	 
	if (init_eaccess(ramdisk_execute_command) != 0) {
		ramdisk_execute_command = NULL;
		prepare_namespace();
	}

	 

	integrity_load_keys();
}
