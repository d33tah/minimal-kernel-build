// SPDX-License-Identifier: GPL-2.0-only
/*
 * x86 APERF/MPERF KHz calculation for
 * /sys/.../cpufreq/scaling_cur_freq
 *
 * Copyright (C) 2017 Intel Corp.
 * Author: Len Brown <len.brown@intel.com>
 */
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/percpu.h>
#include <linux/rcupdate.h>
#include <linux/sched/isolation.h>
#include <linux/sched/topology.h>
#include <linux/smp.h>
#include <linux/syscore_ops.h>

#include <asm/cpu.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>

#include "cpu.h"

struct aperfmperf {
	seqcount_t	seq;
	unsigned long	last_update;
	u64		acnt;
	u64		mcnt;
	u64		aperf;
	u64		mperf;
};

static DEFINE_PER_CPU_SHARED_ALIGNED(struct aperfmperf, cpu_samples) = {
	.seq = SEQCNT_ZERO(cpu_samples.seq)
};

static void init_counter_refs(void)
{
	u64 aperf, mperf;

	rdmsrl(MSR_IA32_APERF, aperf);
	rdmsrl(MSR_IA32_MPERF, mperf);

	this_cpu_write(cpu_samples.aperf, aperf);
	this_cpu_write(cpu_samples.mperf, mperf);
}

static inline void bp_init_freq_invariance(void) { }
static inline void scale_freq_tick(u64 acnt, u64 mcnt) { }

void arch_scale_freq_tick(void)
{
	struct aperfmperf *s = this_cpu_ptr(&cpu_samples);
	u64 acnt, mcnt, aperf, mperf;

	if (!cpu_feature_enabled(X86_FEATURE_APERFMPERF))
		return;

	rdmsrl(MSR_IA32_APERF, aperf);
	rdmsrl(MSR_IA32_MPERF, mperf);
	acnt = aperf - s->aperf;
	mcnt = mperf - s->mperf;

	s->aperf = aperf;
	s->mperf = mperf;

	raw_write_seqcount_begin(&s->seq);
	s->last_update = jiffies;
	s->acnt = acnt;
	s->mcnt = mcnt;
	raw_write_seqcount_end(&s->seq);

	scale_freq_tick(acnt, mcnt);
}

/*
 * Discard samples older than the define maximum sample age of 20ms. There
 * is no point in sending IPIs in such a case. If the scheduler tick was
 * not running then the CPU is either idle or isolated.
 */
#define MAX_SAMPLE_AGE ((unsigned long)HZ / 50)

unsigned int arch_freq_get_on_cpu(int cpu)
{
	struct aperfmperf *s = per_cpu_ptr(&cpu_samples, cpu);
	unsigned int seq, freq;
	unsigned long last;
	u64 acnt, mcnt;

	if (!cpu_feature_enabled(X86_FEATURE_APERFMPERF))
		goto fallback;

	do {
		seq = raw_read_seqcount_begin(&s->seq);
		last = s->last_update;
		acnt = s->acnt;
		mcnt = s->mcnt;
	} while (read_seqcount_retry(&s->seq, seq));

	/*
	 * Bail on invalid count and when the last update was too long ago,
	 * which covers idle and NOHZ full CPUs.
	 */
	if (!mcnt || (jiffies - last) > MAX_SAMPLE_AGE)
		goto fallback;

	return div64_u64((cpu_khz * acnt), mcnt);

fallback:
	freq = cpufreq_quick_get(cpu);
	return freq ? freq : cpu_khz;
}

static int __init bp_init_aperfmperf(void)
{
	if (!cpu_feature_enabled(X86_FEATURE_APERFMPERF))
		return 0;

	init_counter_refs();
	bp_init_freq_invariance();
	return 0;
}
early_initcall(bp_init_aperfmperf);

void ap_init_aperfmperf(void)
{
	if (cpu_feature_enabled(X86_FEATURE_APERFMPERF))
		init_counter_refs();
}
