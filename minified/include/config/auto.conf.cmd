deps_config := \
	arch/x86/Kconfig.debug \
	kernel/rcu/Kconfig.debug \
	lib/Kconfig.kfence \
	lib/Kconfig.kasan \
	mm/Kconfig.debug \
	lib/Kconfig.kcsan \
	lib/Kconfig.ubsan \
	lib/Kconfig.kgdb \
	lib/Kconfig.debug \
	lib/vdso/Kconfig \
	kernel/dma/Kconfig \
	lib/xz/Kconfig \
	lib/crypto/Kconfig \
	lib/math/Kconfig \
	lib/Kconfig \
	security/Kconfig \
	fs/Kconfig \
	drivers/video/console/Kconfig \
	drivers/video/Kconfig \
	drivers/rtc/Kconfig \
	drivers/input/serio/Kconfig \
	drivers/input/keyboard/Kconfig \
	drivers/input/Kconfig \
	drivers/clocksource/Kconfig \
	drivers/tty/Kconfig \
	drivers/char/Kconfig \
	drivers/base/Kconfig \
	drivers/Kconfig \
	mm/Kconfig \
	fs/Kconfig.binfmt \
	kernel/Kconfig.freezer \
	kernel/Kconfig.locks \
	arch/x86/Kconfig.assembler \
	arch/x86/kvm/Kconfig \
	kernel/power/Kconfig \
	kernel/Kconfig.hz \
	arch/x86/events/Kconfig \
	arch/x86/Kconfig.cpu \
	arch/x86/xen/Kconfig \
	arch/x86/Kconfig \
	arch/Kconfig \
	usr/Kconfig \
	kernel/rcu/Kconfig \
	kernel/Kconfig.preempt \
	kernel/time/Kconfig \
	kernel/irq/Kconfig \
	init/Kconfig \
	scripts/Kconfig.include \
	Kconfig \

include/config/auto.conf: $(deps_config)

ifneq "$(ARCH)" "x86"
include/config/auto.conf: FORCE
endif
ifneq "$(KERNELVERSION)" "5.19.0-rc8"
include/config/auto.conf: FORCE
endif
ifneq "$(CC)" "gcc"
include/config/auto.conf: FORCE
endif
ifneq "$(LD)" "ld"
include/config/auto.conf: FORCE
endif
ifneq "$(srctree)" "."
include/config/auto.conf: FORCE
endif
ifneq "$(CC_VERSION_TEXT)" "gcc (Debian 12.2.0-14) 12.2.0"
include/config/auto.conf: FORCE
endif
ifneq "$(NM)" "nm"
include/config/auto.conf: FORCE
endif
ifneq "$(OBJCOPY)" "objcopy"
include/config/auto.conf: FORCE
endif
ifneq "$(PAHOLE)" "pahole"
include/config/auto.conf: FORCE
endif
ifneq "$(SRCARCH)" "x86"
include/config/auto.conf: FORCE
endif
ifneq "$(AR)" "ar"
include/config/auto.conf: FORCE
endif

$(deps_config): ;
