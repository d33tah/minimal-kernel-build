/* NTP stubs - minimal includes */
#include <linux/timex.h>
#include <linux/ktime.h>
#include "ntp_internal.h"

static u64 tick_length_stub = ((u64)TICK_NSEC << 32);

void ntp_clear(void) { }
u64 ntp_tick_length(void) { return tick_length_stub; }
ktime_t ntp_get_next_leap(void) { return KTIME_MAX; }
int second_overflow(time64_t secs) { return 0; }
void __init ntp_init(void) { }
