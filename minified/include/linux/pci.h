#ifndef LINUX_PCI_H
#define LINUX_PCI_H

#include <linux/mod_devicetable.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/kobject.h>
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/acpi.h>

#ifdef CONFIG_X86
#include <asm/pci.h>
#endif

#include <linux/dma-mapping.h>

#endif
