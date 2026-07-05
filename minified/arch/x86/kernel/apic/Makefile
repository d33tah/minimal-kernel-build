# SPDX-License-Identifier: GPL-2.0
#
# Makefile for local APIC drivers and for the IO-APIC code
#

# Leads to non-deterministic coverage that is not a function of syscall inputs.
# In particualr, smp_apic_timer_interrupt() is called in random places.
KCOV_INSTRUMENT		:= n

obj-$(CONFIG_X86_LOCAL_APIC)	+= apic.o apic_common.o apic_noop.o ipi.o vector.o

obj-$(CONFIG_X86_IO_APIC)	+= io_apic.o
obj-$(CONFIG_PCI_MSI)		+= msi.o
obj-$(CONFIG_SMP)		+= ipi.o

# APIC probe will depend on the listing order here
obj-$(CONFIG_X86_BIGSMP)	+= bigsmp_32.o

# For 32bit, probe_32 need to be listed last
obj-$(CONFIG_X86_LOCAL_APIC)	+= probe_$(BITS).o
