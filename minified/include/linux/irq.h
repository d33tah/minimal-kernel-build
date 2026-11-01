/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_IRQ_H
#define _LINUX_IRQ_H

struct irq_desc;
typedef void (*irq_flow_handler_t)(struct irq_desc *desc);
#include <linux/interrupt.h>
#include <asm/io.h>

#include <linux/slab.h>

struct irq_data {
	unsigned int irq;
	struct irq_chip *chip;
};

/**
 * struct irq_chip - hardware interrupt chip descriptor
 *
 * @name:		name for /proc/interrupts
 * @irq_startup:	start up the interrupt (defaults to ->enable if NULL)
 * @irq_shutdown:	shut down the interrupt (defaults to ->disable if NULL)
 * @irq_enable:		enable the interrupt (defaults to chip->unmask if NULL)
 * @irq_disable:	disable the interrupt
 * @irq_ack:		start of a new interrupt
 * @irq_mask:		mask an interrupt source
 * @irq_mask_ack:	ack and mask an interrupt source
 * @irq_unmask:		unmask an interrupt source
 * @irq_eoi:		end of interrupt
 * @irq_set_affinity:	Set the CPU affinity on SMP machines. If the force
 *			argument is true, it tells the driver to
 *			unconditionally apply the affinity setting. Sanity
 *			checks against the supplied affinity mask are not
 *			required. This is used for CPU hotplug where the
 *			target CPU is not yet set in the cpu_online_mask.
 * @irq_retrigger:	resend an IRQ to the CPU
 * @irq_set_type:	set the flow type (IRQ_TYPE_LEVEL/etc.) of an IRQ
 * @irq_set_wake:	enable/disable power-management wake-on of an IRQ
 * @irq_bus_lock:	function to lock access to slow bus (i2c) chips
 * @irq_bus_sync_unlock:function to sync and unlock slow bus (i2c) chips
 * @irq_cpu_online:	configure an interrupt source for a secondary CPU
 * @irq_cpu_offline:	un-configure an interrupt source for a secondary CPU
 * @irq_suspend:	function called from core code on suspend once per
 *			chip, when one or more interrupts are installed
 * @irq_resume:		function called from core code on resume once per chip,
 *			when one ore more interrupts are installed
 * @irq_pm_shutdown:	function called from core code on shutdown once per chip
 * @irq_calc_mask:	Optional function to set irq_data.mask for special cases
 * @irq_print_chip:	optional to print special chip info in show_interrupts
 * @irq_request_resources:	optional to request resources before calling
 *				any other callback related to this irq
 * @irq_release_resources:	optional to release resources acquired with
 *				irq_request_resources
 * @irq_compose_msi_msg:	optional to compose message content for MSI
 * @irq_write_msi_msg:	optional to write message content for MSI
 * @irq_get_irqchip_state:	return the internal state of an interrupt
 * @irq_set_irqchip_state:	set the internal state of a interrupt
 * @irq_set_vcpu_affinity:	optional to target a vCPU in a virtual machine
 * @ipi_send_single:	send a single IPI to destination cpus
 * @ipi_send_mask:	send an IPI to destination cpus in cpumask
 * @irq_nmi_setup:	function called from core code before enabling an NMI
 * @irq_nmi_teardown:	function called from core code after disabling an NMI
 * @flags:		chip specific flags
 */
struct irq_chip {
	const char *name;
	void (*irq_mask_ack)(struct irq_data *data);
	void (*irq_unmask)(struct irq_data *data);
};

#include <linux/irqdesc.h>

/*
 * Pick up the arch-dependent methods:
 */
#include <asm/hw_irq.h>

extern void handle_level_irq(struct irq_desc *desc);
extern struct irq_chip dummy_irq_chip;

extern void irq_set_chip_and_handler(unsigned int irq, struct irq_chip *chip,
				     irq_flow_handler_t handle);

#endif
