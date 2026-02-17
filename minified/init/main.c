
#include <linux/types.h>
#include <linux/extable.h>
#include <linux/binfmts.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/initrd.h>
#include <linux/memblock.h>

#include <linux/console.h>
#include <linux/percpu.h>
#include <linux/security.h>
#include <linux/smp.h>
#include <linux/rcupdate.h>
#include <linux/srcu.h>
#include <linux/cpu.h>
#include <linux/cgroup.h>
#include <linux/interrupt.h>
#include <linux/rmap.h>
#include <linux/pid_namespace.h>
#include <linux/vmalloc.h>

#include <linux/module.h>
extern void driver_init(void);
extern void timekeeping_init(void);
#include <linux/kthread.h>
#include <linux/sched.h>
extern void sched_init(void);
#include <linux/signal.h>
#include <linux/idr.h>
#include <linux/slab.h>

#include <linux/sched/clock.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/list.h>
#include <linux/proc_ns.h>

#include <linux/cache.h>
#include <linux/jump_label.h>
#include <linux/mem_encrypt.h>
int __init init_mount(const char *dev_name, const char *dir_name,
		      const char *type_page, unsigned long flags,
		      void *data_page);
int __init init_chroot(const char *filename);
int __init init_eaccess(const char *filename);
int __init init_dup(struct file *file);

#include <asm/processor.h>
extern void check_bugs(void);
#include <asm/setup.h>
#include <asm/sections.h>

static int kernel_init(void *);

extern void init_IRQ(void);
extern void radix_tree_init(void);

/* Merged from init/calibrate.c - delay loop calibration stub */
static DEFINE_PER_CPU(unsigned long, cpu_loops_per_jiffy) = { 0 };
void calibrate_delay(void)
{
	unsigned long lpj = 12500000;
	int this_cpu = smp_processor_id();
	if (per_cpu(cpu_loops_per_jiffy, this_cpu))
		lpj = per_cpu(cpu_loops_per_jiffy, this_cpu);
	per_cpu(cpu_loops_per_jiffy, this_cpu) = lpj;
	loops_per_jiffy = lpj;
}

enum system_states system_state __read_mostly;

#define MAX_INIT_ARGS CONFIG_INIT_ENV_ARG_LIMIT
#define MAX_INIT_ENVS CONFIG_INIT_ENV_ARG_LIMIT

extern void time_init(void);

char __initdata boot_command_line[COMMAND_LINE_SIZE];
char *saved_command_line;
static char *static_command_line;

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
static const char *panic_later, *panic_param;

extern const struct obs_kernel_param __setup_start[], __setup_end[];

unsigned long loops_per_jiffy = (1 << 12);

static void __init repair_env_string(char *param, char *val)
{
	if (val) {
		if (val == param + strlen(param) + 1)
			val[-1] = '=';
		else if (val == param + strlen(param) + 2) {
			val[-2] = '=';
			memmove(val - 1, val, strlen(val) + 1);
		} else
			BUG();
	}
}

static int __init set_init_arg(char *param, char *val, const char *unused,
			       void *arg)
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

static int __init unknown_bootoption(char *param, char *val, const char *unused,
				     void *arg)
{
	size_t len = strlen(param);
	const struct obs_kernel_param *p;
	bool had_early_param = false;

	repair_env_string(param, val);

	p = __setup_start;
	do {
		int n = strlen(p->str);
		if (parameqn(param, p->str, n)) {
			if (p->early) {
				if (param[n] == '\0' || param[n] == '=')
					had_early_param = true;
			} else if (!p->setup_func) {
				pr_warn("Parameter %s is obsolete, ignored\n",
					p->str);
				return 0;
			} else if (p->setup_func(param + n))
				return 0;
		}
		p++;
	} while (p < __setup_end);

	if (had_early_param)
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
			if (!strncmp(param, envp_init[i], len + 1))
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

	pid = kernel_thread(kthreadd, NULL, CLONE_FS | CLONE_FILES);
	rcu_read_lock();
	kthreadd_task = find_task_by_pid_ns(pid, &init_pid_ns);
	rcu_read_unlock();

	system_state = SYSTEM_SCHEDULING;

	complete(&kthreadd_done);

	schedule_preempt_disabled();

	cpu_startup_entry(CPUHP_ONLINE);
}

static int __init do_early_param(char *param, char *val, const char *unused,
				 void *arg)
{
	const struct obs_kernel_param *p;

	for (p = __setup_start; p < __setup_end; p++) {
		if ((p->early && parameq(param, p->str)) ||
		    (strcmp(param, "console") == 0 &&
		     strcmp(p->str, "earlycon") == 0)) {
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

void __init poking_init(void); /* in arch/x86/mm/init.c */
void __init arch_post_acpi_subsys_init(void); /* in arch/x86/kernel/process.c */

void __init trap_init(void); /* in arch/x86/kernel/traps.c */

asmlinkage __visible void __init __no_sanitize_address start_kernel(void)
{
	char *command_line;
	char *after_dashes;

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
	{
		size_t len = strlen(boot_command_line) + 1;
		saved_command_line = memblock_alloc(len, SMP_CACHE_BYTES);
		if (!saved_command_line)
			panic("Failed to allocate saved_command_line\n");
		static_command_line = memblock_alloc(len, SMP_CACHE_BYTES);
		if (!static_command_line)
			panic("Failed to allocate static_command_line\n");
		strcpy(saved_command_line, boot_command_line);
		strcpy(static_command_line, command_line);
	}
	setup_per_cpu_areas();

	build_all_zonelists(NULL);

	pr_notice("Kernel command line: %s\n", saved_command_line);

	jump_label_init();
	parse_early_param();
	after_dashes = parse_args("Booting kernel", static_command_line,
				  __start___param,
				  __stop___param - __start___param, -1, -1,
				  NULL, &unknown_bootoption);
	if (!IS_ERR_OR_NULL(after_dashes))
		parse_args("Setting init args", after_dashes, NULL, 0, -1, -1,
			   NULL, set_init_arg);

	vfs_caches_init_early();
	sort_main_extable();
	trap_init();
	init_mem_debugging_and_hardening();
	mem_init();
	kmem_cache_init();
	vmalloc_init();
	sched_init();

	if (WARN(!irqs_disabled(),
		 "Interrupts were enabled *very* early, fixing it\n"))
		local_irq_disable();
	radix_tree_init();

	rcu_init();

	early_irq_init();
	init_IRQ();
	srcu_init();
	timekeeping_init();
	time_init();

	WARN(!irqs_disabled(), "Interrupts were enabled early\n");

	local_irq_enable();
	console_init();

	if (panic_later)
		panic("Too many boot %s vars at `%s'", panic_later,
		      panic_param);

	if (initrd_start && !initrd_below_start_ok &&
	    page_to_pfn(virt_to_page((void *)initrd_start)) < min_low_pfn) {
		pr_crit("initrd overwritten (0x%08lx < 0x%08lx) - disabling it.\n",
			page_to_pfn(virt_to_page((void *)initrd_start)),
			min_low_pfn);
		initrd_start = 0;
	}
	setup_per_cpu_pageset();
	calibrate_delay();
	pid_idr_init();
	anon_vma_init();
	cred_init();
	fork_init();
	proc_caches_init();
	vfs_caches_init();
	pagecache_init();
	signals_init();
	poking_init();
	check_bugs();
	arch_post_acpi_subsys_init();
	rest_init(); /* was arch_call_rest_init() */
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
	__initcall0_start, __initcall1_start, __initcall2_start,
	__initcall3_start, __initcall4_start, __initcall5_start,
	__initcall6_start, __initcall7_start, __initcall_end,
};

static const char *initcall_level_names[] __initdata = {
	"pure", "core", "postcore", "arch", "subsys", "fs", "device", "late",
};

static int __init ignore_unknown_bootoption(char *param, char *val,
					    const char *unused, void *arg)
{
	return 0;
}

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

	{
		initcall_entry_t *fn;
		for (fn = __initcall_start; fn < __initcall0_start; fn++)
			do_one_initcall(initcall_from_entry(fn));
	}
	memblock_discard(); /* page_alloc_init_late inlined */

	{
		int level;
		initcall_entry_t *fn;
		static char command_line[256];
		size_t len = strlen(saved_command_line);

		driver_init();

		if (len >= sizeof(command_line))
			len = sizeof(command_line) - 1;

		for (level = 0; level < ARRAY_SIZE(initcall_levels) - 1;
		     level++) {
			memcpy(command_line, saved_command_line, len);
			command_line[len] = '\0';
			parse_args(initcall_level_names[level], command_line,
				   __start___param,
				   __stop___param - __start___param, level,
				   level, NULL, ignore_unknown_bootoption);
			for (fn = initcall_levels[level];
			     fn < initcall_levels[level + 1]; fn++)
				do_one_initcall(initcall_from_entry(fn));
		}
	}
	console_on_rootfs();

	if (init_eaccess(ramdisk_execute_command) != 0) {
		ramdisk_execute_command = NULL;
		prepare_namespace();
	}
}
