#include <linux/jiffies.h>

#include "timekeeping.h"

void do_timer(unsigned long ticks)
{
	jiffies_64 += ticks;
}
