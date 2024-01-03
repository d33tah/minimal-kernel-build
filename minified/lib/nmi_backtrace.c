// SPDX-License-Identifier: GPL-2.0
/*
 *  NMI backtrace support
 *
 * Gratuitously copied from arch/x86/kernel/apic/hw_nmi.c by Russell King,
 * with the following header:
 *
 *  HW NMI watchdog support
 *
 *  started by Don Zickus, Copyright (C) 2010 Red Hat, Inc.
 *
 *  Arch specific calls to support NMI watchdog
 *
 *  Bits copied from original nmi.c file
 */
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/kprobes.h>
#include <linux/nmi.h>
#include <linux/cpu.h>
#include <linux/sched/debug.h>

