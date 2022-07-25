# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux kernel device drivers.
#
# 15 Sep 2000, Christoph Hellwig <hch@infradead.org>
#

obj-y				+= irqchip/
obj-y				+= bus/

obj-$(CONFIG_GENERIC_PHY)	+= phy/

obj-$(CONFIG_PINCTRL)		+= pinctrl/
obj-y				+= pwm/

obj-y				+= pci/

obj-$(CONFIG_PARISC)		+= parisc/
obj-$(CONFIG_RAPIDIO)		+= rapidio/
obj-y				+= video/
obj-y				+= idle/

# IPMI must come before ACPI in order to provide IPMI opregion support
obj-y				+= char/ipmi/

obj-$(CONFIG_ACPI)		+= acpi/

# PnP must come after ACPI since it will eventually need to check if acpi
# was used and do nothing if so
obj-$(CONFIG_PNP)		+= pnp/
obj-y				+= amba/

obj-y				+= clk/
# Many drivers will want to use DMA so this has to be made available
# really early.
obj-$(CONFIG_DMADEVICES)	+= dma/

obj-y				+= soc/

obj-y				+= virtio/


# reset controllers early, since gpu drivers might rely on them to initialize
obj-$(CONFIG_RESET_CONTROLLER)	+= reset/

# tty/ comes before char/ so that the VT console is the boot-time
# default.
obj-y				+= tty/
obj-y				+= char/

# iommu/ comes before gpu as gpu are using iommu controllers
obj-y				+= iommu/

# gpu/ comes after char for AGP vs DRM startup and after iommu
obj-y				+= gpu/


# i810fb and intelfb depend on char/agp/
obj-$(CONFIG_FB_I810)           += video/fbdev/i810/
obj-$(CONFIG_FB_INTEL)          += video/fbdev/intelfb/

obj-y				+= base/ block/ misc/ mfd/ nfc/
obj-$(CONFIG_DMA_SHARED_BUFFER) += dma-buf/
obj-$(CONFIG_NUBUS)		+= nubus/
obj-y				+= cxl/
obj-y				+= macintosh/
obj-y				+= scsi/
obj-y				+= nvme/
obj-$(CONFIG_TARGET_CORE)	+= target/
obj-$(CONFIG_MTD)		+= mtd/
obj-$(CONFIG_HSI)		+= hsi/
obj-y				+= firewire/
obj-$(CONFIG_VFIO)		+= vfio/
obj-y				+= auxdisplay/
obj-$(CONFIG_DIO)		+= dio/
obj-$(CONFIG_SBUS)		+= sbus/
obj-$(CONFIG_ZORRO)		+= zorro/
obj-$(CONFIG_ATA_OVER_ETH)	+= block/aoe/
obj-$(CONFIG_PARIDE) 		+= block/paride/
obj-$(CONFIG_TC)		+= tc/
obj-$(CONFIG_USB_PHY)		+= usb/
obj-$(CONFIG_USB)		+= usb/
obj-$(CONFIG_USB_SUPPORT)	+= usb/
obj-$(CONFIG_PCI)		+= usb/
obj-$(CONFIG_USB_GADGET)	+= usb/
obj-$(CONFIG_OF)		+= usb/
obj-$(CONFIG_SERIO)		+= input/serio/
obj-$(CONFIG_GAMEPORT)		+= input/gameport/
obj-$(CONFIG_INPUT)		+= input/
obj-$(CONFIG_RTC_LIB)		+= rtc/
obj-y				+= i2c/ i3c/ media/
obj-$(CONFIG_PPS)		+= pps/
obj-y				+= ptp/
obj-$(CONFIG_W1)		+= w1/
obj-y				+= power/
obj-$(CONFIG_HWMON)		+= hwmon/
obj-$(CONFIG_THERMAL)		+= thermal/
obj-$(CONFIG_MD)		+= md/
obj-$(CONFIG_ACCESSIBILITY)	+= accessibility/
obj-$(CONFIG_ISDN)		+= isdn/
obj-$(CONFIG_CPU_FREQ)		+= cpufreq/
obj-$(CONFIG_CPU_IDLE)		+= cpuidle/
obj-y				+= mmc/
obj-y				+= ufs/
obj-$(CONFIG_MEMSTICK)		+= memstick/
obj-$(CONFIG_NEW_LEDS)		+= leds/
obj-$(CONFIG_INFINIBAND)	+= infiniband/
obj-y				+= firmware/
obj-$(CONFIG_CRYPTO)		+= crypto/
obj-$(CONFIG_SUPERH)		+= sh/
obj-y				+= clocksource/
obj-$(CONFIG_HID)		+= hid/
obj-$(CONFIG_PPC_PS3)		+= ps3/
obj-$(CONFIG_STAGING)		+= staging/
obj-y				+= platform/


# Virtualization drivers
obj-$(CONFIG_VIRT_DRIVERS)	+= virt/

obj-$(CONFIG_PM_DEVFREQ)	+= devfreq/
obj-$(CONFIG_MEMORY)		+= memory/
obj-$(CONFIG_IIO)		+= iio/
obj-$(CONFIG_VME_BUS)		+= vme/
obj-$(CONFIG_IPACK_BUS)		+= ipack/
obj-$(CONFIG_NTB)		+= ntb/
obj-$(CONFIG_PERF_EVENTS)	+= perf/
obj-$(CONFIG_CORESIGHT)		+= hwtracing/coresight/
obj-y				+= hwtracing/intel_th/
obj-$(CONFIG_STM)		+= hwtracing/stm/
obj-$(CONFIG_TEE)		+= tee/
obj-$(CONFIG_INTERCONNECT)	+= interconnect/
obj-$(CONFIG_PECI)		+= peci/
