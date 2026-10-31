# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

obj-y     = fork.o panic.o \
	    cpu.o exit.o softirq.o \
	    capability.o user.o \
	    workqueue.o pid.o \
	    extable.o \
	    nsproxy.o \
	    cred.o


# Prevents flicker of uninteresting __do_softirq()/__local_bh_disable_ip()
# in coverage traces.
# Avoid KCSAN instrumentation in softirq ("No shared variables, all the data
# are CPU local" => assume no data races), to reduce overhead in interrupts.
# These are called from save_stack_trace() on slub debug path,
# and produce insane amounts of uninteresting coverage.

obj-y += sched/
obj-y += locking/
obj-y += printk/
obj-y += irq/
obj-y += rcu/
obj-y += entry/

obj-y += time/
# obj-$(CONFIG_GENERIC_ISA_DMA) += dma.o  # deleted








