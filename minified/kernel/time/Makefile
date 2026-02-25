# SPDX-License-Identifier: GPL-2.0
obj-y += timer.o
# hrtimer.o removed - entire hrtimer subsystem dead
obj-y += timekeeping.o clocksource.o jiffies.o
# ntp.o removed - functions inlined into timekeeping.c
# timeconv.o removed
# posix-stubs.o removed - had no symbols (only includes)
# clockevents.o, tick-common.o removed - all functions were dead (only comments remained)

obj-$(CONFIG_GENERIC_SCHED_CLOCK)		+= sched_clock.o
obj-$(CONFIG_LEGACY_TIMER_TICK)			+= tick-legacy.o
# vsyscall.o removed - update_vsyscall was empty stub
