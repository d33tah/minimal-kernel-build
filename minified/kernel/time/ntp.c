/* NTP stubs - minimal includes */
#include <linux/timex.h>
#include <linux/ktime.h>
#include "ntp_internal.h"

static u64 tick_length_stub = ((u64)TICK_NSEC << 32);

void ntp_clear(void) { }
u64 ntp_tick_length(void) { return tick_length_stub; }
void __init ntp_init(void) { }
