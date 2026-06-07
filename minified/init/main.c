
#include <linux/extable.h>
#include <linux/binfmts.h>
#include <linux/file.h>
extern unsigned long initrd_start, initrd_end;
extern phys_addr_t phys_initrd_start;
extern unsigned long phys_initrd_size;
#include <linux/memblock.h>

#include <linux/cpu.h>
#include <linux/interrupt.h>
extern void anon_vma_init(void);
#include <linux/pid_namespace.h>

/* driver_init inlined: only bdi_init needed */
#include <linux/backing-dev.h>
extern void timekeeping_init(void);
#include <linux/kthread.h>
extern void sched_init(void);

#include <linux/sched/task_stack.h>

int __init init_chroot(const char *filename);
int __init init_eaccess(const char *filename);
int __init init_dup(struct file *file);

extern void check_bugs(void);
#include <asm/setup.h>

static int kernel_init(void *);

extern void init_IRQ(void);
extern void radix_tree_init(void);

enum system_states system_state __read_mostly;

#define MAX_INIT_ARGS CONFIG_INIT_ENV_ARG_LIMIT
#define MAX_INIT_ENVS CONFIG_INIT_ENV_ARG_LIMIT

char __initdata boot_command_line[COMMAND_LINE_SIZE];

static char *ramdisk_execute_command = "/init";

bool static_key_initialized __read_mostly;

static const char *argv_init[MAX_INIT_ARGS + 2] = {
	"init",
	NULL,
};
const char *envp_init[MAX_INIT_ENVS + 2] = {
	"HOME=/",
	"TERM=linux",
	NULL,
};
unsigned long loops_per_jiffy = (1 << 12);

static __initdata DECLARE_COMPLETION(kthreadd_done);

static noinline void __ref rest_init(void)
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
}

void __init trap_init(void); /* in arch/x86/kernel/traps.c */

asmlinkage __visible void __init __no_sanitize_address start_kernel(void)
{
	char *command_line;

	set_task_stack_end_magic(&init_task);

	local_irq_disable();

	boot_cpu_init();

	/* Early VGA Hello World - before memory-hungry init */
	{
		volatile char *vga = (volatile char *)0xC00B8000;
		const char *msg = "Hello, World!";
		int i;
		for (i = 0; msg[i]; i++) {
			vga[i * 2] = msg[i];
			vga[i * 2 + 1] = 0x0f;
		}
	}

	pr_notice("%s", linux_banner);
	setup_arch(&command_line);
	setup_per_cpu_areas();

	build_all_zonelists(NULL);

	pr_notice("Kernel command line: %s\n", boot_command_line);

	jump_label_init();
	parse_early_param();

	vfs_caches_init_early();
	sort_main_extable();
	trap_init();
	mem_init();
	kmem_cache_init();
	sched_init();

	if (WARN(!irqs_disabled(),
		 "Interrupts were enabled *very* early, fixing it\n"))
		local_irq_disable();
	radix_tree_init();

	rcu_init();

	early_irq_init();
	init_IRQ();
	timekeeping_init();

	WARN(!irqs_disabled(), "Interrupts were enabled early\n");

	local_irq_enable();

	setup_per_cpu_pageset();
	loops_per_jiffy = 12500000;
	pid_idr_init();
	anon_vma_init();
	cred_init();
	fork_init();
	proc_caches_init();
	vfs_caches_init();
	pagecache_init();
	check_bugs();
	rest_init(); /* was arch_call_rest_init() */
	prevent_tail_call_optimization();
}

static int __init_or_module do_one_initcall(initcall_t fn)
{
	return fn();
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

static int run_init_process(const char *init_filename)
{
	argv_init[0] = init_filename;
	pr_info("Run %s as init process\n", init_filename);
	return kernel_execve(init_filename, argv_init, envp_init);
}

static noinline void __init kernel_init_freeable(void);

/* free_initmem provided by arch/x86/mm/init.c */

static int __ref kernel_init(void *unused)
{
	int ret;

	wait_for_completion(&kthreadd_done);

	kernel_init_freeable();

	system_state = SYSTEM_FREEING_INITMEM;
	free_initmem();
	rcu_barrier();

	system_state = SYSTEM_RUNNING;

	ret = run_init_process(ramdisk_execute_command);
	if (!ret)
		return 0;
	panic("Failed to execute %s (error %d).", ramdisk_execute_command, ret);
}

static void __init console_on_rootfs(void)
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

	{
		initcall_entry_t *fn;
		for (fn = __initcall_start; fn < __initcall0_start; fn++)
			do_one_initcall(initcall_from_entry(fn));
	}
	memblock_discard(); /* page_alloc_init_late inlined */

	{
		initcall_entry_t *fn;

		{
			struct backing_dev_info *bdi = &noop_backing_dev_info;
			bdi->dev = NULL;
			kref_init(&bdi->refcnt);
			INIT_LIST_HEAD(&bdi->bdi_list);
			INIT_LIST_HEAD(&bdi->wb_list);
			init_waitqueue_head(&bdi->wb_waitq);
		}

		for (fn = initcall_levels[0];
		     fn < initcall_levels[ARRAY_SIZE(initcall_levels) - 1];
		     fn++)
			do_one_initcall(initcall_from_entry(fn));
	}
	console_on_rootfs();

	if (init_eaccess(ramdisk_execute_command) != 0) {
		ramdisk_execute_command = NULL;
		prepare_namespace();
	}
}
