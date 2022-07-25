# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux kernel device drivers.
#
# 15 Sep 2000, Christoph Hellwig <hch@infradead.org>
#

obj-$(CONFIG_GENERIC_PHY)	+= phy/

obj-$(CONFIG_PINCTRL)		+= pinctrl/

obj-$(CONFIG_PARISC)		+= parisc/
obj-$(CONFIG_RAPIDIO)		+= rapidio/
obj-y				+= video/

# IPMI must come before ACPI in order to provide IPMI opregion support
obj-y				+= char/ipmi/

obj-$(CONFIG_ACPI)		+= acpi/

# PnP must come after ACPI since it will eventually need to check if acpi
# was used and do nothing if so
obj-$(CONFIG_PNP)		+= pnp/

# Many drivers will want to use DMA so this has to be made available
# really early.
obj-$(CONFIG_DMADEVICES)	+= dma/

# reset controllers early, since gpu drivers might rely on them to initialize
obj-$(CONFIG_RESET_CONTROLLER)	+= reset/

# tty/ comes before char/ so that the VT console is the boot-time
# default.
obj-y				+= tty/
obj-y				+= char/

# i810fb and intelfb depend on char/agp/
obj-$(CONFIG_FB_I810)           += video/fbdev/i810/
obj-$(CONFIG_FB_INTEL)          += video/fbdev/intelfb/

obj-y				+= base/
obj-$(CONFIG_DMA_SHARED_BUFFER) += dma-buf/
obj-$(CONFIG_NUBUS)		+= nubus/
obj-$(CONFIG_TARGET_CORE)	+= target/
obj-$(CONFIG_HSI)		+= hsi/
obj-$(CONFIG_VFIO)		+= vfio/
obj-$(CONFIG_DIO)		+= dio/
obj-$(CONFIG_SBUS)		+= sbus/
obj-$(CONFIG_ZORRO)		+= zorro/
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
obj-$(CONFIG_PPS)		+= pps/
obj-$(CONFIG_W1)		+= w1/
obj-$(CONFIG_HWMON)		+= hwmon/
obj-$(CONFIG_THERMAL)		+= thermal/
obj-$(CONFIG_MD)		+= md/
obj-$(CONFIG_ACCESSIBILITY)	+= accessibility/
obj-$(CONFIG_ISDN)		+= isdn/
obj-$(CONFIG_CPU_FREQ)		+= cpufreq/
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


# Virtualization drivers
obj-$(CONFIG_VIRT_DRIVERS)	+= virt/

obj-$(CONFIG_PM_DEVFREQ)	+= devfreq/
obj-$(CONFIG_MEMORY)		+= memory/
obj-$(CONFIG_IIO)		+= iio/
obj-$(CONFIG_VME_BUS)		+= vme/
obj-$(CONFIG_IPACK_BUS)		+= ipack/
obj-$(CONFIG_NTB)		+= ntb/
obj-$(CONFIG_TEE)		+= tee/
obj-$(CONFIG_INTERCONNECT)	+= interconnect/
obj-$(CONFIG_PECI)		+= peci/
