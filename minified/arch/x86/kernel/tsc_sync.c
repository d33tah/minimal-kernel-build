// SPDX-License-Identifier: GPL-2.0
/*
 * check TSC synchronization.
 *
 * Copyright (C) 2006, Red Hat, Inc., Ingo Molnar
 *
 * We check whether all boot CPUs have their TSC's synchronized,
 * print a warning if not and turn off the TSC clock-source.
 *
 * The warp-check is point-to-point between two CPUs, the CPU
 * initiating the bootup is the 'source CPU', the freshly booting
 * CPU is the 'target CPU'.
 *
 * Only two CPUs may participate - they can enter in any order.
 * ( The serial nature of the boot logic and the CPU hotplug lock
 *   protects against more than 2 CPUs entering this code. )
 */
#include <linux/smp.h>
#include <asm-generic/percpu.h>
#include <asm/cpufeatures.h>
#include <asm/tsc.h>

#include "asm-generic/int-ll64.h"
#include "asm-generic/param.h"
#include "asm/cache.h"
#include "asm/cpufeature.h"
#include "asm/cpufeatures.h"
#include "asm/msr-index.h"
#include "asm/msr.h"
#include "linux/compiler.h"
#include "linux/cpumask.h"
#include "linux/init.h"
#include "linux/jiffies.h"
#include "linux/printk.h"
#include "linux/stddef.h"
#include "linux/timer.h"
#include "linux/types.h"

struct tsc_adjust {
	s64		bootval;
	s64		adjusted;
	unsigned long	nextcheck;
	bool		warned;
};

static DEFINE_PER_CPU(struct tsc_adjust, tsc_adjust);
static struct timer_list tsc_sync_check_timer;

/*
 * TSC's on different sockets may be reset asynchronously.
 * This may cause the TSC ADJUST value on socket 0 to be NOT 0.
 */
bool __read_mostly tsc_async_resets;

void mark_tsc_async_resets(char *reason)
{
	if (tsc_async_resets)
		return;
	tsc_async_resets = true;
	pr_info("tsc: Marking TSC async resets true due to %s\n", reason);
}

void tsc_verify_tsc_adjust(bool resume)
{
	struct tsc_adjust *adj = this_cpu_ptr(&tsc_adjust);
	s64 curval;

	if (!boot_cpu_has(X86_FEATURE_TSC_ADJUST))
		return;

	/* Skip unnecessary error messages if TSC already unstable */
	if (check_tsc_unstable())
		return;

	/* Rate limit the MSR check */
	if (!resume && time_before(jiffies, adj->nextcheck))
		return;

	adj->nextcheck = jiffies + HZ;

	rdmsrl(MSR_IA32_TSC_ADJUST, curval);
	if (adj->adjusted == curval)
		return;

	/* Restore the original value */
	wrmsrl(MSR_IA32_TSC_ADJUST, adj->adjusted);

	if (!adj->warned || resume) {
		pr_warn(FW_BUG "TSC ADJUST differs: CPU%u %lld --> %lld. Restoring\n",
			smp_processor_id(), adj->adjusted, curval);
		adj->warned = true;
	}
}

/*
 * Normally the tsc_sync will be checked every time system enters idle
 * state, but there is still caveat that a system won't enter idle,
 * either because it's too busy or configured purposely to not enter
 * idle.
 *
 * So setup a periodic timer (every 10 minutes) to make sure the check
 * is always on.
 */

#define SYNC_CHECK_INTERVAL		(HZ * 600)

static void tsc_sync_check_timer_fn(struct timer_list *unused)
{
	int next_cpu;

	tsc_verify_tsc_adjust(false);

	/* Run the check for all onlined CPUs in turn */
	next_cpu = cpumask_next(raw_smp_processor_id(), cpu_online_mask);
	if (next_cpu >= nr_cpu_ids)
		next_cpu = cpumask_first(cpu_online_mask);

	tsc_sync_check_timer.expires += SYNC_CHECK_INTERVAL;
	add_timer_on(&tsc_sync_check_timer, next_cpu);
}

static int __init start_sync_check_timer(void)
{
	if (!cpu_feature_enabled(X86_FEATURE_TSC_ADJUST) || tsc_clocksource_reliable)
		return 0;

	timer_setup(&tsc_sync_check_timer, tsc_sync_check_timer_fn, 0);
	tsc_sync_check_timer.expires = jiffies + SYNC_CHECK_INTERVAL;
	add_timer(&tsc_sync_check_timer);

	return 0;
}
late_initcall(start_sync_check_timer);

static void tsc_sanitize_first_cpu(struct tsc_adjust *cur, s64 bootval,
				   unsigned int cpu, bool bootcpu)
{
	/*
	 * First online CPU in a package stores the boot value in the
	 * adjustment value. This value might change later via the sync
	 * mechanism. If that fails we still can yell about boot values not
	 * being consistent.
	 *
	 * On the boot cpu we just force set the ADJUST value to 0 if it's
	 * non zero. We don't do that on non boot cpus because physical
	 * hotplug should have set the ADJUST register to a value > 0 so
	 * the TSC is in sync with the already running cpus.
	 *
	 * Also don't force the ADJUST value to zero if that is a valid value
	 * for socket 0 as determined by the system arch.  This is required
	 * when multiple sockets are reset asynchronously with each other
	 * and socket 0 may not have an TSC ADJUST value of 0.
	 */
	if (bootcpu && bootval != 0) {
		if (likely(!tsc_async_resets)) {
			pr_warn(FW_BUG "TSC ADJUST: CPU%u: %lld force to 0\n",
				cpu, bootval);
			wrmsrl(MSR_IA32_TSC_ADJUST, 0);
			bootval = 0;
		} else {
			pr_info("TSC ADJUST: CPU%u: %lld NOT forced to 0\n",
				cpu, bootval);
		}
	}
	cur->adjusted = bootval;
}

bool __init tsc_store_and_check_tsc_adjust(bool bootcpu)
{
	struct tsc_adjust *cur = this_cpu_ptr(&tsc_adjust);
	s64 bootval;

	if (!boot_cpu_has(X86_FEATURE_TSC_ADJUST))
		return false;

	/* Skip unnecessary error messages if TSC already unstable */
	if (check_tsc_unstable())
		return false;

	rdmsrl(MSR_IA32_TSC_ADJUST, bootval);
	cur->bootval = bootval;
	cur->nextcheck = jiffies + HZ;
	tsc_sanitize_first_cpu(cur, bootval, smp_processor_id(), bootcpu);
	return false;
}

