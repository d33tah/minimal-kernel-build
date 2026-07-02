
#define DEBUG		 

#include <linux/types.h>
#include <linux/extable.h>
#include <linux/proc_fs.h>
#include <linux/binfmts.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
/* end stackprotector.h */
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/memblock.h>

#include <linux/console.h>
#include <linux/percpu.h>
#include <linux/security.h>
#include <linux/smp.h>
#include <linux/rcupdate.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/rmap.h>
#include <linux/vmalloc.h>
#include <linux/pid_namespace.h>

/* --- 2025-12-08 00:37 --- padata.h stubbed out */
#include <linux/device/driver.h>
#include <linux/kthread.h>
#include <linux/sched.h>
extern void sched_init(void);
extern void sched_init_smp(void);
#include <linux/signal.h>
#include <linux/file.h>
#include <linux/slab.h>

#include <linux/sched/clock.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/list.h>

/* --- 2025-12-08 00:40 --- integrity.h stubbed out */
#include <linux/cache.h>
#include <linux/jump_label.h>
#include <linux/init_syscalls.h>

#include <asm/bugs.h>
#include <asm/setup.h>



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

static char *execute_command;
static char *ramdisk_execute_command = "/init";

bool static_key_initialized __read_mostly;


static const char *argv_init[MAX_INIT_ARGS+2] = { "init", NULL, };
const char *envp_init[MAX_INIT_ENVS+2] = { "HOME=/", "TERM=linux", NULL, };

extern const struct obs_kernel_param __setup_start[], __setup_end[];

unsigned long loops_per_jiffy = (1<<12);

/* The debug/quiet/loglevel/bootconfig early_param handlers were empty stubs
 * that only ran when their option appeared on the boot cmdline; the test boot
 * passes an empty cmdline so they never fired (do_mounts/gbpages precedent). */
#define exit_boot_config()	do {} while (0)

/*
 * The runtime-dead cmdline-parse cluster was fully removed. parse_args() had
 * been reduced to a `return NULL;` no-op (empty single-shot cmdline), and every
 * caller discarded its result, so the function and all its call sites (here plus
 * parse_early_param / do_initcall_level) were provably behaviour-neutral and are
 * gone; the `unknown` bootoption callbacks it once dispatched went with it.
 */
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


static void __init setup_command_line(char *command_line)
{
	size_t len;

	len = strlen(boot_command_line) + 1;

	saved_command_line = memblock_alloc(len, SMP_CACHE_BYTES);
	if (!saved_command_line)
		panic("%s: Failed to allocate %zu bytes\n", __func__, len);

	static_command_line = memblock_alloc(len, SMP_CACHE_BYTES);
	if (!static_command_line)
		panic("%s: Failed to allocate %zu bytes\n", __func__, len);

	strcpy(saved_command_line, boot_command_line);
	strcpy(static_command_line, command_line);
}


static __initdata DECLARE_COMPLETION(kthreadd_done);

noinline void __ref rest_init(void)
{
	struct task_struct *tsk;
	int pid;

	pid = user_mode_thread(kernel_init, NULL, CLONE_FS);
	 
	rcu_read_lock();
	tsk = find_task_by_pid_ns(pid, &init_pid_ns);
	tsk->flags |= PF_NO_SETAFFINITY;
	set_cpus_allowed_ptr(tsk, cpumask_of(smp_processor_id()));
	rcu_read_unlock();

	pid = kernel_thread(kthreadd, NULL, CLONE_FS | CLONE_FILES);
	rcu_read_lock();
	kthreadd_task = find_task_by_pid_ns(pid, &init_pid_ns);
	rcu_read_unlock();

	 
	system_state = SYSTEM_SCHEDULING;

	complete(&kthreadd_done);

	 
	schedule_preempt_disabled();
	 
	cpu_startup_entry(CPUHP_ONLINE);
}

void __init parse_early_param(void)
{
	static int done __initdata;
	static char tmp_cmdline[COMMAND_LINE_SIZE] __initdata;

	if (done)
		return;

	 
	strlcpy(tmp_cmdline, boot_command_line, COMMAND_LINE_SIZE);
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

static void __init mm_init(void)
{

	mem_init();
	mem_init_print_info();
	kmem_cache_init();

	pgtable_init();
	vmalloc_init();
}


void __init __weak arch_call_rest_init(void)
{
	rest_init();
}

asmlinkage __visible void __init __no_sanitize_address start_kernel(void)
{
	char *command_line;

	set_task_stack_end_magic(&init_task);
	smp_setup_processor_id();

	local_irq_disable();
	early_boot_irqs_disabled = true;

	 
	boot_cpu_init();
	page_address_init();
	pr_notice("%s", linux_banner);
	setup_arch(&command_line);
	setup_command_line(command_line);
	setup_per_cpu_areas();
	smp_prepare_boot_cpu();

	build_all_zonelists(NULL);
	page_alloc_init();

	pr_notice("Kernel command line: %s\n", saved_command_line);
	 
	jump_label_init();
	parse_early_param();

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


	workqueue_init_early();

	rcu_init();


	early_irq_init();
	init_IRQ();
	init_timers();
	/* hrtimers_init() removed: only initialized never-read per-cpu hrtimer_bases */
	softirq_init();
	timekeeping_init();
	time_init();


	WARN(!irqs_disabled(), "Interrupts were enabled early\n");

	early_boot_irqs_disabled = false;
	local_irq_enable();

	console_init();

	mem_encrypt_init();

	if (initrd_start &&
	    page_to_pfn(virt_to_page((void *)initrd_start)) < min_low_pfn) {
		pr_crit("initrd overwritten (0x%08lx < 0x%08lx) - disabling it.\n",
		    page_to_pfn(virt_to_page((void *)initrd_start)),
		    min_low_pfn);
		initrd_start = 0;
	}
	setup_per_cpu_pageset();
	if (late_time_init)
		late_time_init();
	sched_clock_init();
	calibrate_delay();
	pid_idr_init();
	anon_vma_init();
	thread_stack_cache_init();
	cred_init();
	fork_init();
	proc_caches_init();
	vfs_caches_init();
	pagecache_init();
	signals_init();
	proc_root_init();

	poking_init();
	check_bugs();

	arch_post_acpi_subsys_init();

	 
	arch_call_rest_init();

	prevent_tail_call_optimization();
}

int __init_or_module do_one_initcall(initcall_t fn)
{
	int count = preempt_count();
	char msgbuf[64];
	int ret;

	ret = fn();

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

static void __init do_initcall_level(int level)
{
	initcall_entry_t *fn;

	for (fn = initcall_levels[level]; fn < initcall_levels[level+1]; fn++)
		do_one_initcall(initcall_from_entry(fn));
}

static void __init do_initcalls(void)
{
	int level;

	for (level = 0; level < ARRAY_SIZE(initcall_levels) - 1; level++)
		do_initcall_level(level);
}

static void __init do_basic_setup(void)
{
	driver_init();
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

static void mark_readonly(void)
{
	rcu_barrier();
	mark_rodata_ro();
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

	system_state = SYSTEM_FREEING_INITMEM;
	exit_boot_config();
	free_initmem();
	mark_readonly();

	system_state = SYSTEM_RUNNING;

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

	/* cad_pid removed - only set, never read */


	workqueue_init();

	do_pre_smp_initcalls();

	sched_init_smp();

	page_alloc_init_late();

	do_basic_setup();

	wait_for_initramfs();
	console_on_rootfs();



}
