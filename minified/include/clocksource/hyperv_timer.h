

#ifndef __CLKSOURCE_HYPERV_TIMER_H
#define __CLKSOURCE_HYPERV_TIMER_H

#include <linux/clocksource.h>
#include <linux/math64.h>
#include <asm/mshyperv.h>

#define HV_MAX_MAX_DELTA_TICKS 0xffffffff
#define HV_MIN_DELTA_TICKS 1

static inline struct ms_hyperv_tsc_page *hv_get_tsc_page(void)
{
	return NULL;
}

static inline u64 hv_read_tsc_page_tsc(const struct ms_hyperv_tsc_page *tsc_pg,
				       u64 *cur_tsc)
{
	return U64_MAX;
}

static inline int hv_stimer_cleanup(unsigned int cpu) { return 0; }
static inline void hv_stimer_legacy_init(unsigned int cpu, int sint) {}
static inline void hv_stimer_legacy_cleanup(unsigned int cpu) {}
static inline void hv_stimer_global_cleanup(void) {}
static inline void hv_stimer0_isr(void) {}


#endif
