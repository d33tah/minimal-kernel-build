// SPDX-License-Identifier: GPL-2.0+
/* Stubbed - unused in minimal kernel */
#include <linux/export.h>
#include <linux/timecounter.h>

void timecounter_init(struct timecounter *tc, const struct cyclecounter *cc, u64 start_tstamp) {}
EXPORT_SYMBOL_GPL(timecounter_init);

u64 timecounter_read(struct timecounter *tc) { return 0; }
EXPORT_SYMBOL_GPL(timecounter_read);

u64 timecounter_cyc2time(const struct timecounter *tc, u64 cycle_tstamp) { return 0; }
EXPORT_SYMBOL_GPL(timecounter_cyc2time);
