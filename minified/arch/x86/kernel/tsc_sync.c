// SPDX-License-Identifier: GPL-2.0
// Stubbed out TSC sync functions - not needed for single CPU "Hello World"

#include <linux/kernel.h>

bool tsc_async_resets;

/* mark_tsc_async_resets removed - unused */

void tsc_verify_tsc_adjust(bool resume)
{
}

void tsc_store_and_check_tsc_adjust(bool bootcpu)
{
}
