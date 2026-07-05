# SPDX-License-Identifier: GPL-2.0
obj-y += time.o timer.o
# hrtimer.o removed: hrtimer_bases per-cpu data was initialized by hrtimers_init
# (sole caller init/main.c) but never read anywhere (no struct hrtimer users,
# no per_cpu(hrtimer_bases) reads outside hrtimer.c) -> whole subsystem inert.
# ntp.o removed: ntp_tick_length() was a const stub (TICK_NSEC<<32); inlined
# into ntp_internal.h (its 3 callers are all in timekeeping.c), killing the TU.
obj-y += timekeeping.o clocksource.o jiffies.o
# timeconv.o removed

# posix timers removed: setitimer/getitimer/timer_*/itimer/most clock_* syscalls
# are unreachable (init does write(2)+exit) and commented out in syscall_32.tbl
# (-> sys_ni). posix-stubs.o keeps only clock_gettime/clock_getres because the
# x86 vDSO fallback still references their __NR_ macros.
obj-y += posix-stubs.o

obj-$(CONFIG_GENERIC_CLOCKEVENTS)		+= clockevents.o tick-common.o
obj-$(CONFIG_GENERIC_SCHED_CLOCK)		+= sched_clock.o
obj-$(CONFIG_TICK_ONESHOT)			+= tick-oneshot.o tick-sched.o
obj-$(CONFIG_LEGACY_TIMER_TICK)			+= tick-legacy.o
obj-$(CONFIG_DEBUG_FS)				+= timekeeping_debug.o
obj-$(CONFIG_TEST_UDELAY)			+= test_udelay.o
obj-$(CONFIG_TIME_NS)				+= namespace.o
obj-$(CONFIG_TEST_CLOCKSOURCE_WATCHDOG)		+= clocksource-wdtest.o
obj-$(CONFIG_TIME_KUNIT_TEST)			+= time_test.o
