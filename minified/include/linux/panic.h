#ifndef _LINUX_PANIC_H
#define _LINUX_PANIC_H

#include <linux/compiler_attributes.h>
#include <linux/types.h>

struct pt_regs;

extern long (*panic_blink)(int state);
__printf(1, 2)
void panic(const char *fmt, ...) __noreturn __cold;
void nmi_panic(struct pt_regs *regs, const char *msg);
extern void oops_enter(void);
extern void oops_exit(void);
/* Removed: oops_may_print - never called */

extern int panic_timeout;
extern unsigned long panic_print;
extern int panic_on_oops;
extern int panic_on_unrecovered_nmi;
extern int panic_on_io_nmi;
extern int panic_on_warn;

extern unsigned long panic_on_taint;
/* Removed: panic_on_taint_nousertaint, sysctl_panic_on_rcu_stall,
   sysctl_max_rcu_stall_to_panic, sysctl_panic_on_stackoverflow - never defined/used */

extern bool crash_kexec_post_notifiers;

extern atomic_t panic_cpu;
#define PANIC_CPU_INVALID	-1

#define TAINT_PROPRIETARY_MODULE	0
#define TAINT_FORCED_MODULE		1
#define TAINT_CPU_OUT_OF_SPEC		2
#define TAINT_FORCED_RMMOD		3
#define TAINT_MACHINE_CHECK		4
#define TAINT_BAD_PAGE			5
#define TAINT_USER			6
#define TAINT_DIE			7
#define TAINT_OVERRIDDEN_ACPI_TABLE	8
#define TAINT_WARN			9
#define TAINT_CRAP			10
#define TAINT_FIRMWARE_WORKAROUND	11
#define TAINT_OOT_MODULE		12
#define TAINT_UNSIGNED_MODULE		13
#define TAINT_SOFTLOCKUP		14
#define TAINT_LIVEPATCH			15
#define TAINT_AUX			16
#define TAINT_RANDSTRUCT		17
#define TAINT_FLAGS_COUNT		18
#define TAINT_FLAGS_MAX			((1UL << TAINT_FLAGS_COUNT) - 1)

enum lockdep_ok {
	LOCKDEP_STILL_OK,
	LOCKDEP_NOW_UNRELIABLE,
};

extern void add_taint(unsigned flag, enum lockdep_ok);

#endif
