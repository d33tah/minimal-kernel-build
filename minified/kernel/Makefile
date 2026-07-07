# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

obj-y     = fork.o panic.o \
	    cpu.o exit.o softirq.o resource.o \
	    capability.o user.o \
	    signal.o workqueue.o pid.o task_work.o \
	    extable.o \
	    kthread.o sys_ni.o nsproxy.o \
	    notifier.o cred.o \
	    range.o ucount.o


# Prevents flicker of uninteresting __do_softirq()/__local_bh_disable_ip()
# in coverage traces.
KCOV_INSTRUMENT_softirq.o := n
# Avoid KCSAN instrumentation in softirq ("No shared variables, all the data
# are CPU local" => assume no data races), to reduce overhead in interrupts.
KCSAN_SANITIZE_softirq.o = n
# These are called from save_stack_trace() on slub debug path,
# and produce insane amounts of uninteresting coverage.
KCOV_INSTRUMENT_extable.o := n

obj-y += sched/
obj-y += locking/
obj-y += power/
obj-y += printk/
obj-y += irq/
obj-y += rcu/
obj-y += dma/
obj-y += entry/

obj-y += time/
# obj-$(CONFIG_GENERIC_ISA_DMA) += dma.o  # deleted


# Always build events/ for stubs
obj-y += events/






