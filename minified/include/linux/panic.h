#ifndef _LINUX_PANIC_H
#define _LINUX_PANIC_H

#include <linux/compiler_attributes.h>
#include <linux/types.h>

struct pt_regs;

/* panic_blink removed - no_blink always returned 0 */
__printf(1, 2)
void panic(const char *fmt, ...) __noreturn __cold;
/* nmi_panic removed - never called */
extern void oops_enter(void);
extern void oops_exit(void);
/* Removed: oops_may_print - never called */

extern int panic_timeout;
/* panic_print removed - only defined, never read */
extern int panic_on_oops;
extern int panic_on_warn;
/* panic_on_unrecovered_nmi, panic_on_io_nmi, panic_on_taint, panic_on_taint_nousertaint,
   sysctl_panic_on_rcu_stall, sysctl_max_rcu_stall_to_panic, sysctl_panic_on_stackoverflow removed */
/* Removed: crash_kexec_post_notifiers - kexec stubs removed */

extern atomic_t panic_cpu;
#define PANIC_CPU_INVALID	-1

#define TAINT_BAD_PAGE			5
#define TAINT_USER			6
#define TAINT_DIE			7

enum lockdep_ok {
	LOCKDEP_STILL_OK,
	LOCKDEP_NOW_UNRELIABLE,
};

extern void add_taint(unsigned flag, enum lockdep_ok);

#endif
