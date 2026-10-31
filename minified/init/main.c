#include <linux/extable.h>
#include <linux/binfmts.h>
#include <linux/delay.h>

#include <linux/console.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/pid_namespace.h>

extern void sched_init(void);
#include <linux/file.h>

#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>

#include <linux/init_syscalls.h>

#include <asm/bugs.h>
#include <asm/time.h>
#include <linux/initrd.h>

static int kernel_init(void *);

bool static_key_initialized __read_mostly;

/* /init is the only program in the built-in initramfs (usr/Makefile). */
static const char *argv_init[] = { "/init", NULL };
static const char *envp_init[] = { "HOME=/", "TERM=linux", NULL };

unsigned long loops_per_jiffy = (1 << 12);

noinline void __ref rest_init(void)
{
	user_mode_thread(kernel_init, NULL, CLONE_FS);

	schedule_preempt_disabled();

	cpu_startup_entry(CPUHP_ONLINE);
}

asmlinkage __visible void __init __no_sanitize_address start_kernel(void)
{
	set_task_stack_end_magic(&init_task);

	local_irq_disable();

	boot_cpu_init();
	setup_arch();

	build_all_zonelists(NULL);

	jump_label_init();

	vfs_caches_init_early();
	sort_main_extable();
	trap_init();
	mem_init();
	kmem_cache_init();

	sched_init();

	if (WARN(!irqs_disabled(),
		 "Interrupts were enabled *very* early, fixing it\n"))
		local_irq_disable();
	xarray_init();

	rcu_init();

	early_irq_init();
	init_IRQ();
	timekeeping_init();

	local_irq_enable();

	console_init();

	time_init();
	calibrate_delay();
	pid_init();
	cred_init();
	fork_init();
	proc_caches_init();
	vfs_caches_init();

	check_bugs();

	rest_init();
}

static int __init do_one_initcall(initcall_t fn)
{
	int count = preempt_count();
	int ret;

	ret = fn();

	/* The "initcall returned with ..." WARN is a no_printk, so only the
	   repairs remain. */
	if (preempt_count() != count)
		preempt_count_set(count);
	if (irqs_disabled())
		local_irq_enable();

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
	__initcall0_start, __initcall1_start, __initcall2_start,
	__initcall3_start, __initcall4_start, __initcall5_start,
	__initcall6_start, __initcall7_start, __initcall_end,
};

static void __init do_initcall_level(int level)
{
	initcall_entry_t *fn;

	for (fn = initcall_levels[level]; fn < initcall_levels[level + 1]; fn++)
		do_one_initcall(initcall_from_entry(fn));
}

static void __init do_initcalls(void)
{
	int level;

	for (level = 0; level < ARRAY_SIZE(initcall_levels) - 1; level++)
		do_initcall_level(level);
}

static void __init do_pre_smp_initcalls(void)
{
	initcall_entry_t *fn;

	for (fn = __initcall_start; fn < __initcall0_start; fn++)
		do_one_initcall(initcall_from_entry(fn));
}

void __init console_on_rootfs(void)
{
	struct file *file = filp_open("/dev/console", O_RDWR, 0);

	if (IS_ERR(file))
		panic("cannot open /dev/console");
	init_dup(file);
	init_dup(file);
	init_dup(file);
	fput(file);
}

static noinline void __init kernel_init_freeable(void)
{
	gfp_allowed_mask = __GFP_BITS_MASK;

	do_pre_smp_initcalls();

	page_alloc_init_late();

	do_initcalls();

	console_on_rootfs();
}

static int __ref kernel_init(void *unused)
{
	kernel_init_freeable();

	rcu_barrier();
	mark_rodata_ro();

	if (!kernel_execve(argv_init[0], argv_init, envp_init))
		return 0;
	panic("cannot execute /init");
}
