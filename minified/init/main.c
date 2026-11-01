// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/init/main.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  GK 2/5/95  -  Changed to support mounting root fs via NFS
 *  Added initrd & change_root: Werner Almesberger & Hans Lermen, Feb '96
 *  Moan early if gcc is old, avoiding bogus kernels - Paul Gortmaker, May '96
 *  Simplified starting of init:  Michael A. Griffith <grif@acm.org>
 */

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

/* /init is the only program in the built-in initramfs (usr/Makefile). */
static const char *argv_init[] = { "/init", NULL };
static const char *envp_init[] = { "HOME=/", "TERM=linux", NULL };

unsigned long loops_per_jiffy = (1 << 12);

static noinline void __ref rest_init(void)
{
	user_mode_thread(kernel_init, NULL, CLONE_FS);

	/*
	 * The boot idle thread must execute schedule()
	 * at least once to get things moving:
	 */
	schedule_preempt_disabled();

	/* Call into cpu_idle with preempt disabled */
	cpu_startup_entry(CPUHP_ONLINE);
}

asmlinkage __visible void __init __no_sanitize_address start_kernel(void)
{
	set_task_stack_end_magic(&init_task);

	local_irq_disable();

	setup_arch();

	vfs_caches_init_early();
	trap_init();
	mem_init();
	kmem_cache_init();

	/*
	 * Set up the scheduler prior starting any interrupts (such as the
	 * timer interrupt). Full topology setup happens at smp_init()
	 * time - but meanwhile we still have a functioning scheduler.
	 */
	sched_init();

	if (WARN(!irqs_disabled(),
		 "Interrupts were enabled *very* early, fixing it\n"))
		local_irq_disable();

	/* init some links before init_ISA_irqs() */
	early_irq_init();
	init_IRQ();

	local_irq_enable();

	console_init();

	time_init();
	calibrate_delay();
	pid_idr_init();
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

extern initcall_entry_t __initcall1_start[];
extern initcall_entry_t __initcall5_start[];
extern initcall_entry_t __initcall_end[];

static initcall_entry_t *initcall_levels[] __initdata = {
	__initcall1_start,
	__initcall5_start,
	__initcall_end,
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

static void __init console_on_rootfs(void)
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
	/* Now the scheduler is fully set up and can do blocking allocations */
	gfp_allowed_mask = __GFP_BITS_MASK;

	do_initcalls();

	console_on_rootfs();
}

static int __ref kernel_init(void *unused)
{
	kernel_init_freeable();

	if (!kernel_execve(argv_init[0], argv_init, envp_init))
		return 0;
	panic("cannot execute /init");
}
