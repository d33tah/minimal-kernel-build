# SPDX-License-Identifier: GPL-2.0
VERSION = 5
PATCHLEVEL = 19
SUBLEVEL = 0
EXTRAVERSION = -rc8

# Builds arch/x86/boot/bzImage (x86_32, clang/LLVM, in the source tree) from
# the frozen .config. Targets: all (default), clean, mrproper.
# 'make V=1' prints the full commands, 'make V=2' also why a target is rebuilt.

PHONY := __all
__all:

# Do not use make's built-in rules and variables
MAKEFLAGS += -rR --no-print-directory

# Avoid funny character set dependencies
unexport LC_ALL
LC_COLLATE=C
LC_NUMERIC=C
export LC_COLLATE LC_NUMERIC

# Avoid interference with shell env settings
unexport GREP_OPTIONS

# Beautify output: $(Q) hides a command unless V=1, $(quiet) selects the
# short "quiet_cmd_*" form of a command.
ifeq ("$(origin V)", "command line")
  KBUILD_VERBOSE = $(V)
endif
ifndef KBUILD_VERBOSE
  KBUILD_VERBOSE = 0
endif

ifeq ($(KBUILD_VERBOSE),1)
  quiet =
  Q =
else
  quiet=quiet_
  Q = @
endif

# If the user is running make -s (silent mode), suppress echoing of
# commands
ifneq ($(findstring s,$(filter-out --%,$(MAKEFLAGS))),)
  quiet=silent_
  KBUILD_VERBOSE = 0
endif

export quiet Q KBUILD_VERBOSE

srctree := .
objtree := .
export srctree objtree

# clean and mrproper need neither the configuration nor the compiler
clean-targets := clean mrproper
need-config := 1
ifneq ($(MAKECMDGOALS),)
ifeq ($(filter-out $(clean-targets), $(MAKECMDGOALS)),)
need-config :=
endif
endif

include $(srctree)/scripts/Kbuild.include

KERNELRELEASE = $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION)
export KERNELRELEASE

ARCH		:= x86
SRCARCH		:= x86

# SHELL used by kbuild
CONFIG_SHELL := sh

HOSTCC	= clang

KBUILD_HOSTCFLAGS   := -Wall -Wmissing-prototypes -Wstrict-prototypes \
		       -O2 -fomit-frame-pointer -std=gnu11 \
		       -Wdeclaration-after-statement $(HOSTCFLAGS)
KBUILD_HOSTLDFLAGS  := $(HOSTLDFLAGS)
KBUILD_HOSTLDLIBS   := $(HOSTLDLIBS)

# The toolchain is always LLVM (the .config is frozen with CONFIG_CC_IS_CLANG
# and CONFIG_LTO_CLANG); CI passes LLVM=1, which changes nothing.
CPP		= $(CC) -E
CC		= clang
LD		= ld.lld
AR		= llvm-ar
NM		= llvm-nm
OBJCOPY		= llvm-objcopy

NOSTDINC_FLAGS :=

# Use USERINCLUDE when you must reference the UAPI directories only.
USERINCLUDE    := \
		-I$(srctree)/arch/$(SRCARCH)/include/uapi \
		-I$(objtree)/arch/$(SRCARCH)/include/generated/uapi \
		-I$(srctree)/include/uapi \
		-I$(objtree)/include/generated/uapi \
                -include $(srctree)/include/linux/compiler-version.h \
                -include $(srctree)/include/linux/kconfig.h

# Use LINUXINCLUDE when you must reference the include/ directory.
LINUXINCLUDE    := \
		-I$(srctree)/arch/$(SRCARCH)/include \
		-I$(objtree)/arch/$(SRCARCH)/include/generated \
		-I$(objtree)/include \
		$(USERINCLUDE)

KBUILD_AFLAGS   := -D__ASSEMBLY__ -fno-PIE
KBUILD_CFLAGS   := -Wall -Wundef -Werror=strict-prototypes -Wno-trigraphs \
		   -fno-strict-aliasing -fno-common -fshort-wchar -fno-PIE \
		   -Werror=implicit-function-declaration -Werror=implicit-int \
		   -Werror=return-type -Wno-format-security \
		   -std=gnu11
KBUILD_CPPFLAGS := -D__KERNEL__
KBUILD_LDFLAGS :=
CLANG_FLAGS :=

export ARCH SRCARCH CONFIG_SHELL HOSTCC KBUILD_HOSTCFLAGS LD CC
export CPP AR NM OBJCOPY UTS_MACHINE MAKE
export KBUILD_HOSTLDFLAGS KBUILD_HOSTLDLIBS

export KBUILD_CPPFLAGS NOSTDINC_FLAGS LINUXINCLUDE OBJCOPYFLAGS KBUILD_LDFLAGS
export KBUILD_CFLAGS KBUILD_AFLAGS

ifdef need-config
include $(srctree)/scripts/Makefile.clang
include $(srctree)/scripts/Makefile.compiler
endif

PHONY += all
__all: all

KBUILD_BUILTIN := 1
export KBUILD_BUILTIN

ifdef need-config
include include/config/auto.conf
endif

# Objects we will link into vmlinux / subdirs we need to visit
core-y		:= init/ usr/ arch/$(SRCARCH)/
drivers-y	:= drivers/
libs-y		:= lib/

all: vmlinux

include $(srctree)/arch/$(SRCARCH)/Makefile

ifdef need-config
# The configuration is frozen in .config; mirror it into include/config/ and
# include/generated/ whenever .config or the generator changes.
include/config/auto.conf include/generated/autoconf.h &: .config $(srctree)/scripts/gen-autoconf.sh
	$(Q)$(kecho) "  GEN     $@"
	$(Q)$(CONFIG_SHELL) $(srctree)/scripts/gen-autoconf.sh
endif

KBUILD_CFLAGS	+= -fno-delete-null-pointer-checks
KBUILD_CFLAGS	+= $(call cc-disable-warning,frame-address,)
KBUILD_CFLAGS	+= $(call cc-disable-warning, format-truncation)
KBUILD_CFLAGS	+= $(call cc-disable-warning, format-overflow)
KBUILD_CFLAGS	+= $(call cc-disable-warning, address-of-packed-member)

KBUILD_CFLAGS += -Os

KBUILD_CFLAGS += -Wframe-larger-than=$(CONFIG_FRAME_WARN)

KBUILD_CFLAGS += -fno-stack-protector

KBUILD_CFLAGS += $(CONFIG_CC_IMPLICIT_FALLTHROUGH)

KBUILD_CPPFLAGS += -Qunused-arguments
# The kernel builds with '-std=gnu11' so use of GNU extensions is acceptable.
KBUILD_CFLAGS += -Wno-gnu

# These warnings generated too much noise in a regular build.
KBUILD_CFLAGS += $(call cc-disable-warning, unused-but-set-variable)
KBUILD_CFLAGS += $(call cc-disable-warning, unused-const-variable)

KBUILD_CFLAGS	+= -fomit-frame-pointer

# While VLAs have been removed, GCC produces unreachable stack probes
# for the randomize_kstack_offset feature. Disable it for all compilers.
KBUILD_CFLAGS	+= $(call cc-option, -fno-stack-clash-protection)

CC_FLAGS_LTO	:= -flto -fvisibility=hidden

# Limit inlining across translation units to reduce binary size
KBUILD_LDFLAGS += -mllvm -import-instr-limit=5

KBUILD_CFLAGS	+= -fno-lto $(CC_FLAGS_LTO)
KBUILD_AFLAGS	+= -fno-lto
export CC_FLAGS_LTO

NOSTDINC_FLAGS += -nostdinc

# warn about C99 declaration after statement
KBUILD_CFLAGS += -Wdeclaration-after-statement

# Variable Length Arrays (VLAs) should not be used anywhere in the kernel
KBUILD_CFLAGS += -Wvla

# disable pointer signed / unsigned warnings
KBUILD_CFLAGS += -Wno-pointer-sign

# In order to make sure new function cast mismatches are not introduced
# in the kernel (to avoid tripping CFI checking), the kernel should be
# globally built with -Wcast-function-type.
KBUILD_CFLAGS += $(call cc-option, -Wcast-function-type)

# disable invalid "can't wrap" optimizations for signed / pointers
KBUILD_CFLAGS	+= -fno-strict-overflow

# Make sure -fstack-check isn't enabled (like gentoo apparently did)
KBUILD_CFLAGS  += -fno-stack-check

# Prohibit date/time macros, which would make the build non-deterministic
KBUILD_CFLAGS   += -Werror=date-time

# enforce correct pointer usage
KBUILD_CFLAGS   += $(call cc-option,-Werror=incompatible-pointer-types)

# change __FILE__ to the relative path from the srctree
KBUILD_CPPFLAGS += $(call cc-option,-fmacro-prefix-map=$(srctree)/=)

include $(srctree)/scripts/Makefile.extrawarn

# Add user supplied CPPFLAGS, AFLAGS and CFLAGS as the last assignments
KBUILD_CPPFLAGS += $(KCPPFLAGS)
KBUILD_AFLAGS   += $(KAFLAGS)
KBUILD_CFLAGS   += $(KCFLAGS)

LDFLAGS_vmlinux += --build-id=sha1

# We never want expected sections to be placed heuristically by the
# linker. All sections should be explicitly named in the linker script.
LDFLAGS_vmlinux += --orphan-handling=warn

core-y			+= kernel/ mm/ fs/ security/

vmlinux-dirs	:= $(patsubst %/,%,$(filter %/, $(core-y) $(drivers-y) $(libs-y)))

build-dirs	:= $(vmlinux-dirs)
clean-dirs	:= $(vmlinux-dirs)

# Externally visible symbols (used by link-vmlinux.sh)
KBUILD_VMLINUX_OBJS := $(head-y) $(patsubst %/,%/built-in.a, $(core-y))
KBUILD_VMLINUX_OBJS += $(addsuffix built-in.a, $(filter %/, $(libs-y)))
KBUILD_VMLINUX_LIBS := $(patsubst %/,%/lib.a, $(libs-y))
KBUILD_VMLINUX_OBJS += $(patsubst %/,%/built-in.a, $(drivers-y))

export KBUILD_VMLINUX_OBJS KBUILD_VMLINUX_LIBS
export KBUILD_LDS          := arch/$(SRCARCH)/kernel/vmlinux.lds

vmlinux-deps := $(KBUILD_LDS) $(KBUILD_VMLINUX_OBJS) $(KBUILD_VMLINUX_LIBS)

# Final link of vmlinux
cmd_link-vmlinux = $(CONFIG_SHELL) $< "$(LD)" "$(KBUILD_LDFLAGS)" "$(LDFLAGS_vmlinux)"

vmlinux: scripts/link-vmlinux.sh $(vmlinux-deps) FORCE
	+$(call if_changed_dep,link-vmlinux)

targets := vmlinux

# The actual objects are generated when descending,
# make sure no implicit rule kicks in
$(sort $(vmlinux-deps)): descend ;

# Things we need to do before we recursively start building the kernel.
# prepare0 generates bounds.h and asm-offsets.h (./Kbuild).
PHONY += prepare prepare0 archprepare

archprepare: archheaders asm-generic \
	include/generated/utsrelease.h include/generated/autoconf.h

prepare0: archprepare
	$(Q)$(MAKE) $(build)=.

prepare: prepare0

# Support for using generic headers in asm-generic
asm-generic := -f $(srctree)/scripts/Makefile.asm-generic obj

PHONY += asm-generic uapi-asm-generic
asm-generic: uapi-asm-generic
	$(Q)$(MAKE) $(asm-generic)=arch/$(SRCARCH)/include/generated/asm \
	generic=include/asm-generic
uapi-asm-generic:
	$(Q)$(MAKE) $(asm-generic)=arch/$(SRCARCH)/include/generated/uapi/asm \
	generic=include/uapi/asm-generic

define filechk_utsrelease.h
	echo \#define UTS_RELEASE \"$(KERNELRELEASE)\"
endef

include/generated/utsrelease.h: FORCE
	$(call filechk,utsrelease.h)

PHONY += archheaders

# make clean     Delete most generated files
# make mrproper  Delete all generated files (the frozen .config is kept)

PHONY += archclean vmlinuxclean

vmlinuxclean:
	$(Q)$(CONFIG_SHELL) $(srctree)/scripts/link-vmlinux.sh clean

clean: archclean vmlinuxclean

mrproper: rm-files := $(wildcard include/config include/generated \
		      arch/$(SRCARCH)/include/generated)

PHONY += mrproper
mrproper: clean
	$(call cmd,rmfiles)

# Handle descending into subdirectories listed in $(build-dirs)
PHONY += descend $(build-dirs)
descend: $(build-dirs)
$(build-dirs): prepare
	$(Q)$(MAKE) $(build)=$@ need-builtin=1

clean-dirs := $(addprefix _clean_, $(clean-dirs))
PHONY += $(clean-dirs) clean
$(clean-dirs):
	$(Q)$(MAKE) $(clean)=$(patsubst _clean_%,%,$@)

clean: $(clean-dirs)
	@find . \( -name .git -prune \) -o \
		\( -name '*.[aios]' -o -name '.*.cmd' -o -name '.*.d' \
		-o -name '.*.tmp' -o -name '.tmp_*' -o -name '*.ll' \) \
		-type f -print | xargs rm -f

quiet_cmd_rmfiles = $(if $(wildcard $(rm-files)),CLEAN   $(wildcard $(rm-files)))
      cmd_rmfiles = rm -rf $(rm-files)

# read saved command lines for existing targets
existing-targets := $(wildcard $(sort $(targets)))

-include $(foreach f,$(existing-targets),$(dir $(f)).$(notdir $(f)).cmd)

PHONY += FORCE
FORCE:

# Declare the contents of the PHONY variable as phony.  We keep that
# information in a variable so we can use it in if_changed and friends.
.PHONY: $(PHONY)
