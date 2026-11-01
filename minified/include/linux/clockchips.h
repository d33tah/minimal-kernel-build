/* SPDX-License-Identifier: GPL-2.0 */
/*  linux/include/linux/clockchips.h
 *
 *  This file contains the structure definitions for clockchips.
 *
 *  If you are not a clockchip, or the time of day code, you should
 *  not be including this file!
 */
#ifndef _LINUX_CLOCKCHIPS_H
#define _LINUX_CLOCKCHIPS_H

#include <linux/cache.h>

/*
 * Possible states of a clock event device.
 *
 * DETACHED:	Device is not used by clockevents core. Initial state or can be
 *		reached from SHUTDOWN.
 * SHUTDOWN:	Device is powered-off. Can be reached from PERIODIC or ONESHOT.
 * PERIODIC:	Device is programmed to generate events periodically. Can be
 *		reached from DETACHED or SHUTDOWN.
 * ONESHOT:	Device is programmed to generate event only once. Can be reached
 *		from DETACHED or SHUTDOWN.
 * ONESHOT_STOPPED: Device was programmed in ONESHOT mode and is temporarily
 *		    stopped.
 */
enum clock_event_state {
	CLOCK_EVT_STATE_DETACHED,
	CLOCK_EVT_STATE_PERIODIC,
};

/*
 * Clock event features
 */
#define CLOCK_EVT_FEAT_PERIODIC 0x000001

/**
 * struct clock_event_device - clock event device descriptor
 * @event_handler:	Assigned by the framework to be called by the low
 *			level handler of the event source
 * @set_next_event:	set next event function using a clocksource delta
 * @set_next_ktime:	set next event function using a direct ktime value
 * @next_event:		local storage for the next event in oneshot mode
 * @max_delta_ns:	maximum delta value in ns
 * @min_delta_ns:	minimum delta value in ns
 * @mult:		nanosecond to cycles multiplier
 * @shift:		nanoseconds to cycles divisor (power of two)
 * @state_use_accessors:current state of the device, assigned by the core code
 * @features:		features
 * @retries:		number of forced programming retries
 * @set_state_periodic:	switch state to periodic
 * @set_state_oneshot:	switch state to oneshot
 * @set_state_oneshot_stopped: switch state to oneshot_stopped
 * @set_state_shutdown:	switch state to shutdown
 * @tick_resume:	resume clkevt device
 * @broadcast:		function to broadcast events
 * @min_delta_ticks:	minimum delta value in ticks stored for reconfiguration
 * @max_delta_ticks:	maximum delta value in ticks stored for reconfiguration
 * @name:		ptr to clock event name
 * @rating:		variable to rate clock event devices
 * @irq:		IRQ number (only for non CPU local devices)
 * @bound_on:		Bound on CPU
 * @cpumask:		cpumask to indicate for which CPUs this device works
 * @list:		list head for the management code
 * @owner:		module reference
 */
struct clock_event_device {
	void (*event_handler)(struct clock_event_device *);
	enum clock_event_state state_use_accessors;
	unsigned int features;
	int (*set_state_periodic)(struct clock_event_device *);
} ____cacheline_aligned;

extern void clockevents_register_device(struct clock_event_device *dev);

#endif
