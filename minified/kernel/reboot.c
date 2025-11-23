 
 

#define pr_fmt(fmt)	"reboot: " fmt

#include <linux/atomic.h>
#include <linux/cpu.h>
#include <linux/ctype.h>
#include <linux/export.h>
#include <linux/kexec.h>
#include <linux/kmod.h>
#include <linux/kmsg_dump.h>
#include <linux/reboot.h>
#include <linux/suspend.h>
#include <linux/syscalls.h>
#include <linux/syscore_ops.h>
#include <linux/uaccess.h>

 

static int C_A_D = 1;
struct pid *cad_pid;

#define DEFAULT_REBOOT_MODE
enum reboot_mode reboot_mode DEFAULT_REBOOT_MODE;
enum reboot_mode panic_reboot_mode = REBOOT_UNDEFINED;

 
int reboot_default = 1;
int reboot_cpu;
enum reboot_type reboot_type = BOOT_ACPI;
int reboot_force;

struct sys_off_handler {
	struct notifier_block nb;
	int (*sys_off_cb)(struct sys_off_data *data);
	void *cb_data;
	enum sys_off_mode mode;
	bool blocking;
	void *list;
};

 
void __weak (*pm_power_off)(void);

 
void emergency_restart(void)
{
	kmsg_dump(KMSG_DUMP_EMERG);
	machine_emergency_restart();
}

void kernel_restart_prepare(char *cmd)
{
	blocking_notifier_call_chain(&reboot_notifier_list, SYS_RESTART, cmd);
	system_state = SYSTEM_RESTART;
	usermodehelper_disable();
	device_shutdown();
}

 
int register_reboot_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&reboot_notifier_list, nb);
}

 
int unregister_reboot_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&reboot_notifier_list, nb);
}

static void devm_unregister_reboot_notifier(struct device *dev, void *res)
{
	WARN_ON(unregister_reboot_notifier(*(struct notifier_block **)res));
}

int devm_register_reboot_notifier(struct device *dev, struct notifier_block *nb)
{
	struct notifier_block **rcnb;
	int ret;

	rcnb = devres_alloc(devm_unregister_reboot_notifier,
			    sizeof(*rcnb), GFP_KERNEL);
	if (!rcnb)
		return -ENOMEM;

	ret = register_reboot_notifier(nb);
	if (!ret) {
		*rcnb = nb;
		devres_add(dev, rcnb);
	} else {
		devres_free(rcnb);
	}

	return ret;
}

 
static ATOMIC_NOTIFIER_HEAD(restart_handler_list);

 
int register_restart_handler(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&restart_handler_list, nb);
}

 
int unregister_restart_handler(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&restart_handler_list, nb);
}

 
void do_kernel_restart(char *cmd)
{
	atomic_notifier_call_chain(&restart_handler_list, reboot_mode, cmd);
}

void migrate_to_reboot_cpu(void)
{
	 
	int cpu = reboot_cpu;

	cpu_hotplug_disable();

	 
	if (!cpu_online(cpu))
		cpu = cpumask_first(cpu_online_mask);

	 
	current->flags |= PF_NO_SETAFFINITY;

	 
	set_cpus_allowed_ptr(current, cpumask_of(cpu));
}

 
void kernel_restart(char *cmd)
{
	kernel_restart_prepare(cmd);
	migrate_to_reboot_cpu();
	syscore_shutdown();
	if (!cmd)
		pr_emerg("Restarting system\n");
	else
		pr_emerg("Restarting system with command '%s'\n", cmd);
	kmsg_dump(KMSG_DUMP_SHUTDOWN);
	machine_restart(cmd);
}

static void kernel_shutdown_prepare(enum system_states state)
{
	blocking_notifier_call_chain(&reboot_notifier_list,
		(state == SYSTEM_HALT) ? SYS_HALT : SYS_POWER_OFF, NULL);
	system_state = state;
	usermodehelper_disable();
	device_shutdown();
}
 
void kernel_halt(void)
{
	kernel_shutdown_prepare(SYSTEM_HALT);
	migrate_to_reboot_cpu();
	syscore_shutdown();
	pr_emerg("System halted\n");
	kmsg_dump(KMSG_DUMP_SHUTDOWN);
	machine_halt();
}

 
static BLOCKING_NOTIFIER_HEAD(power_off_prep_handler_list);

 
static ATOMIC_NOTIFIER_HEAD(power_off_handler_list);

/* Stubbed sys_off_handler infrastructure - not used externally */
struct sys_off_handler *
register_sys_off_handler(enum sys_off_mode mode,
			 int priority,
			 int (*callback)(struct sys_off_data *data),
			 void *cb_data)
{
	return NULL;
}

void unregister_sys_off_handler(struct sys_off_handler *handler)
{
}

/* Stub: devm_register_* not needed for minimal kernel */
int devm_register_sys_off_handler(struct device *dev,
				  enum sys_off_mode mode,
				  int priority,
				  int (*callback)(struct sys_off_data *data),
				  void *cb_data)
{
	return -ENOSYS;
}

int devm_register_power_off_handler(struct device *dev,
				    int (*callback)(struct sys_off_data *data),
				    void *cb_data)
{
	return -ENOSYS;
}

int devm_register_restart_handler(struct device *dev,
				  int (*callback)(struct sys_off_data *data),
				  void *cb_data)
{
	return -ENOSYS;
}

/* Stub: platform_power_off not needed for minimal kernel */
int register_platform_power_off(void (*power_off)(void))
{
	return -ENOSYS;
}

void unregister_platform_power_off(void (*power_off)(void))
{
}

static int legacy_pm_power_off(struct sys_off_data *data)
{
	if (pm_power_off)
		pm_power_off();

	return NOTIFY_DONE;
}

static void do_kernel_power_off_prepare(void)
{
	blocking_notifier_call_chain(&power_off_prep_handler_list, 0, NULL);
}

 
void do_kernel_power_off(void)
{
	struct sys_off_handler *sys_off = NULL;

	 
	if (pm_power_off)
		sys_off = register_sys_off_handler(SYS_OFF_MODE_POWER_OFF,
						   SYS_OFF_PRIO_DEFAULT,
						   legacy_pm_power_off, NULL);

	atomic_notifier_call_chain(&power_off_handler_list, 0, NULL);

	unregister_sys_off_handler(sys_off);
}

 
bool kernel_can_power_off(void)
{
	return !atomic_notifier_call_chain_is_empty(&power_off_handler_list) ||
		pm_power_off;
}

 
void kernel_power_off(void)
{
	kernel_shutdown_prepare(SYSTEM_POWER_OFF);
	do_kernel_power_off_prepare();
	migrate_to_reboot_cpu();
	syscore_shutdown();
	pr_emerg("Power down\n");
	kmsg_dump(KMSG_DUMP_SHUTDOWN);
	machine_power_off();
}

DEFINE_MUTEX(system_transition_mutex);

 
SYSCALL_DEFINE4(reboot, int, magic1, int, magic2, unsigned int, cmd,
		void __user *, arg)
{
	struct pid_namespace *pid_ns = task_active_pid_ns(current);
	char buffer[256];
	int ret = 0;

	 
	if (!ns_capable(pid_ns->user_ns, CAP_SYS_BOOT))
		return -EPERM;

	 
	if (magic1 != LINUX_REBOOT_MAGIC1 ||
			(magic2 != LINUX_REBOOT_MAGIC2 &&
			magic2 != LINUX_REBOOT_MAGIC2A &&
			magic2 != LINUX_REBOOT_MAGIC2B &&
			magic2 != LINUX_REBOOT_MAGIC2C))
		return -EINVAL;

	 
	ret = reboot_pid_ns(pid_ns, cmd);
	if (ret)
		return ret;

	 
	if ((cmd == LINUX_REBOOT_CMD_POWER_OFF) && !kernel_can_power_off())
		cmd = LINUX_REBOOT_CMD_HALT;

	mutex_lock(&system_transition_mutex);
	switch (cmd) {
	case LINUX_REBOOT_CMD_RESTART:
		kernel_restart(NULL);
		break;

	case LINUX_REBOOT_CMD_CAD_ON:
		C_A_D = 1;
		break;

	case LINUX_REBOOT_CMD_CAD_OFF:
		C_A_D = 0;
		break;

	case LINUX_REBOOT_CMD_HALT:
		kernel_halt();
		do_exit(0);

	case LINUX_REBOOT_CMD_POWER_OFF:
		kernel_power_off();
		do_exit(0);
		break;

	case LINUX_REBOOT_CMD_RESTART2:
		ret = strncpy_from_user(&buffer[0], arg, sizeof(buffer) - 1);
		if (ret < 0) {
			ret = -EFAULT;
			break;
		}
		buffer[sizeof(buffer) - 1] = '\0';

		kernel_restart(buffer);
		break;



	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&system_transition_mutex);
	return ret;
}

static void deferred_cad(struct work_struct *dummy)
{
	kernel_restart(NULL);
}

 
void ctrl_alt_del(void)
{
	static DECLARE_WORK(cad_work, deferred_cad);

	if (C_A_D)
		schedule_work(&cad_work);
	else
		kill_cad_pid(SIGINT, 1);
}

#define POWEROFF_CMD_PATH_LEN  256
static char poweroff_cmd[POWEROFF_CMD_PATH_LEN] = "/sbin/poweroff";
static const char reboot_cmd[] = "/sbin/reboot";

static int run_cmd(const char *cmd)
{
	char **argv;
	static char *envp[] = {
		"HOME=/",
		"PATH=/sbin:/bin:/usr/sbin:/usr/bin",
		NULL
	};
	int ret;
	argv = argv_split(GFP_KERNEL, cmd, NULL);
	if (argv) {
		ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);
		argv_free(argv);
	} else {
		ret = -ENOMEM;
	}

	return ret;
}

static int __orderly_reboot(void)
{
	int ret;

	ret = run_cmd(reboot_cmd);

	if (ret) {
		emergency_sync();
		kernel_restart(NULL);
	}

	return ret;
}

static int __orderly_poweroff(bool force)
{
	int ret;

	ret = run_cmd(poweroff_cmd);

	if (ret && force) {
		 
		emergency_sync();
		kernel_power_off();
	}

	return ret;
}

static bool poweroff_force;

static void poweroff_work_func(struct work_struct *work)
{
	__orderly_poweroff(poweroff_force);
}

static DECLARE_WORK(poweroff_work, poweroff_work_func);

 
void orderly_poweroff(bool force)
{
	if (force)  
		poweroff_force = true;
	schedule_work(&poweroff_work);
}

static void reboot_work_func(struct work_struct *work)
{
	__orderly_reboot();
}

static DECLARE_WORK(reboot_work, reboot_work_func);

 
void orderly_reboot(void)
{
	schedule_work(&reboot_work);
}

 
static void hw_failure_emergency_poweroff_func(struct work_struct *work)
{
	 
	pr_emerg("Hardware protection timed-out. Trying forced poweroff\n");
	kernel_power_off();

	 
	pr_emerg("Hardware protection shutdown failed. Trying emergency restart\n");
	emergency_restart();
}

static DECLARE_DELAYED_WORK(hw_failure_emergency_poweroff_work,
			    hw_failure_emergency_poweroff_func);

 
static void hw_failure_emergency_poweroff(int poweroff_delay_ms)
{
	if (poweroff_delay_ms <= 0)
		return;
	schedule_delayed_work(&hw_failure_emergency_poweroff_work,
			      msecs_to_jiffies(poweroff_delay_ms));
}

 
void hw_protection_shutdown(const char *reason, int ms_until_forced)
{
	static atomic_t allow_proceed = ATOMIC_INIT(1);

	pr_emerg("HARDWARE PROTECTION shutdown (%s)\n", reason);

	 
	if (!atomic_dec_and_test(&allow_proceed))
		return;

	 
	hw_failure_emergency_poweroff(ms_until_forced);
	orderly_poweroff(true);
}

static int __init reboot_setup(char *str)
{
	for (;;) {
		enum reboot_mode *mode;

		 
		reboot_default = 0;

		if (!strncmp(str, "panic_", 6)) {
			mode = &panic_reboot_mode;
			str += 6;
		} else {
			mode = &reboot_mode;
		}

		switch (*str) {
		case 'w':
			*mode = REBOOT_WARM;
			break;

		case 'c':
			*mode = REBOOT_COLD;
			break;

		case 'h':
			*mode = REBOOT_HARD;
			break;

		case 's':
			 
			str += str[1] == 'm' && str[2] == 'p' ? 3 : 1;

			if (isdigit(str[0])) {
				int cpu = simple_strtoul(str, NULL, 0);

				if (cpu >= num_possible_cpus()) {
					pr_err("Ignoring the CPU number in reboot= option. "
					"CPU %d exceeds possible cpu number %d\n",
					cpu, num_possible_cpus());
					break;
				}
				reboot_cpu = cpu;
			} else
				*mode = REBOOT_SOFT;
			break;

		case 'g':
			*mode = REBOOT_GPIO;
			break;

		case 'b':
		case 'a':
		case 'k':
		case 't':
		case 'e':
		case 'p':
			reboot_type = *str;
			break;

		case 'f':
			reboot_force = 1;
			break;
		}

		str = strchr(str, ',');
		if (str)
			str++;
		else
			break;
	}
	return 1;
}
__setup("reboot=", reboot_setup);

