# SPDX-License-Identifier: GPL-2.0
VERSION = 5
PATCHLEVEL = 19
SUBLEVEL = 0
EXTRAVERSION = -rc8
NAME = Superb Owl


$(if $(filter __%, $(MAKECMDGOALS)), \
	$(error targets prefixed with '__' are only for internal use))

# That's our default target when none is given on the command line
PHONY := __all
__all:


ifneq ($(sub_make_done),1)

# Do not use make's built-in rules and variables
# (this increases performance and avoids hard-to-debug behaviour)
MAKEFLAGS += -rR

# Avoid funny character set dependencies
unexport LC_ALL
LC_COLLATE=C
LC_NUMERIC=C
export LC_COLLATE LC_NUMERIC

# Avoid interference with shell env settings
unexport GREP_OPTIONS


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


# Do we want to change the working directory?
ifeq ("$(origin O)", "command line")
  KBUILD_OUTPUT := $(O)
endif

ifneq ($(KBUILD_OUTPUT),)
# Make's built-in functions such as $(abspath ...), $(realpath ...) cannot
# expand a shell special character '~'. We use a somewhat tedious way here.
abs_objtree := $(shell mkdir -p $(KBUILD_OUTPUT) && cd $(KBUILD_OUTPUT) && pwd)
$(if $(abs_objtree),, \
     $(error failed to create output directory "$(KBUILD_OUTPUT)"))

# $(realpath ...) resolves symlinks
abs_objtree := $(realpath $(abs_objtree))
else
abs_objtree := $(CURDIR)
endif # ifneq ($(KBUILD_OUTPUT),)

ifeq ($(abs_objtree),$(CURDIR))
# Suppress "Entering directory ..." unless we are changing the work directory.
MAKEFLAGS += --no-print-directory
else
need-sub-make := 1
endif

this-makefile := $(lastword $(MAKEFILE_LIST))
abs_srctree := $(realpath $(dir $(this-makefile)))

ifneq ($(words $(subst :, ,$(abs_srctree))), 1)
$(error source directory cannot contain spaces or colons)
endif

ifneq ($(abs_srctree),$(abs_objtree))
# Look for make include files relative to root of kernel src
#
# --included-dir is added for backward compatibility, but you should not rely on
# it. Please add $(srctree)/ prefix to include Makefiles in the source tree.
MAKEFLAGS += --include-dir=$(abs_srctree)
endif

export abs_srctree abs_objtree
export sub_make_done := 1

ifeq ($(need-sub-make),1)

PHONY += $(MAKECMDGOALS) __sub-make

$(filter-out $(this-makefile), $(MAKECMDGOALS)) __all: __sub-make
	@:

# Invoke a second make in the output directory, passing relevant variables
__sub-make:
	$(Q)$(MAKE) -C $(abs_objtree) -f $(abs_srctree)/Makefile $(MAKECMDGOALS)

endif # need-sub-make
endif # sub_make_done

# We process the rest of the Makefile if this is the final invocation of make
ifeq ($(need-sub-make),)

# Do not print "Entering directory ...",
# but we want to display it when entering to the output directory
# so that IDEs/editors are able to understand relative filenames.
MAKEFLAGS += --no-print-directory

ifeq ($(abs_srctree),$(abs_objtree))
        # building in the source tree
        srctree := .
	building_out_of_srctree :=
else
        ifeq ($(abs_srctree)/,$(dir $(abs_objtree)))
                # building in a subdirectory of the source tree
                srctree := ..
        else
                srctree := $(abs_srctree)
        endif
	building_out_of_srctree := 1
endif

objtree		:= .
VPATH		:= $(srctree)

export building_out_of_srctree srctree objtree VPATH


version_h := include/generated/uapi/linux/version.h

clean-targets := %clean mrproper
no-dot-config-targets := $(clean-targets) \
			 $(version_h) headers headers_% archheaders archscripts \
			 %asm-generic kernelversion outputmakefile
no-compiler-targets := $(no-dot-config-targets) kernelrelease image_name
no-sync-config-targets := $(no-dot-config-targets) kernelrelease \
			  image_name
config-build	:=
mixed-build	:=
need-config	:= 1
need-compiler	:= 1
may-sync-config	:= 1

ifneq ($(filter $(no-dot-config-targets), $(MAKECMDGOALS)),)
	ifeq ($(filter-out $(no-dot-config-targets), $(MAKECMDGOALS)),)
		need-config :=
	endif
endif

ifneq ($(filter $(no-compiler-targets), $(MAKECMDGOALS)),)
	ifeq ($(filter-out $(no-compiler-targets), $(MAKECMDGOALS)),)
		need-compiler :=
	endif
endif

ifneq ($(filter $(no-sync-config-targets), $(MAKECMDGOALS)),)
	ifeq ($(filter-out $(no-sync-config-targets), $(MAKECMDGOALS)),)
		may-sync-config :=
	endif
endif

ifneq ($(filter %config,$(MAKECMDGOALS)),)
	config-build := 1
	ifneq ($(words $(MAKECMDGOALS)),1)
		mixed-build := 1
	endif
endif

# For "make -j clean all", "make -j mrproper defconfig all", etc.
ifneq ($(filter $(clean-targets),$(MAKECMDGOALS)),)
        ifneq ($(filter-out $(clean-targets),$(MAKECMDGOALS)),)
		mixed-build := 1
        endif
endif

ifdef mixed-build
# ===========================================================================
# We're called with mixed targets (*config and build targets).
# Handle them one by one.

PHONY += $(MAKECMDGOALS) __build_one_by_one

$(MAKECMDGOALS): __build_one_by_one
	@:

__build_one_by_one:
	$(Q)set -e; \
	for i in $(MAKECMDGOALS); do \
		$(MAKE) -f $(srctree)/Makefile $$i; \
	done

else # !mixed-build

include $(srctree)/scripts/Kbuild.include

# Read KERNELRELEASE from include/config/kernel.release (if it exists)
KERNELRELEASE = $(shell cat include/config/kernel.release 2> /dev/null)
KERNELVERSION = $(VERSION)$(if $(PATCHLEVEL),.$(PATCHLEVEL)$(if $(SUBLEVEL),.$(SUBLEVEL)))$(EXTRAVERSION)
export VERSION PATCHLEVEL SUBLEVEL KERNELRELEASE KERNELVERSION

include $(srctree)/scripts/subarch.include

ARCH		?= $(SUBARCH)

# Architecture as present in compile.h
UTS_MACHINE 	:= $(ARCH)
SRCARCH 	:= $(ARCH)

# Additional ARCH settings for x86
ifeq ($(ARCH),i386)
        SRCARCH := x86
endif
ifeq ($(ARCH),x86_64)
        SRCARCH := x86
endif



export cross_compiling :=
ifneq ($(SRCARCH),$(SUBARCH))
cross_compiling := 1
endif

KCONFIG_CONFIG	?= .config
export KCONFIG_CONFIG

# SHELL used by kbuild
CONFIG_SHELL := sh

HOST_LFS_CFLAGS := $(shell getconf LFS_CFLAGS 2>/dev/null)
HOST_LFS_LDFLAGS := $(shell getconf LFS_LDFLAGS 2>/dev/null)
HOST_LFS_LIBS := $(shell getconf LFS_LIBS 2>/dev/null)

ifneq ($(LLVM),)
ifneq ($(filter %/,$(LLVM)),)
LLVM_PREFIX := $(LLVM)
else ifneq ($(filter -%,$(LLVM)),)
LLVM_SUFFIX := $(LLVM)
endif

HOSTCC	= $(LLVM_PREFIX)clang$(LLVM_SUFFIX)
HOSTCXX	= $(LLVM_PREFIX)clang++$(LLVM_SUFFIX)
else
HOSTCC	= gcc
HOSTCXX	= g++
endif
HOSTPKG_CONFIG	= pkg-config

KBUILD_USERHOSTCFLAGS := -Wall -Wmissing-prototypes -Wstrict-prototypes \
			 -O2 -fomit-frame-pointer -std=gnu11 \
			 -Wdeclaration-after-statement
KBUILD_USERCFLAGS  := $(KBUILD_USERHOSTCFLAGS) $(USERCFLAGS)
KBUILD_USERLDFLAGS := $(USERLDFLAGS)

KBUILD_HOSTCFLAGS   := $(KBUILD_USERHOSTCFLAGS) $(HOST_LFS_CFLAGS) $(HOSTCFLAGS)
KBUILD_HOSTCXXFLAGS := -Wall -O2 $(HOST_LFS_CFLAGS) $(HOSTCXXFLAGS)
KBUILD_HOSTLDFLAGS  := $(HOST_LFS_LDFLAGS) $(HOSTLDFLAGS)
KBUILD_HOSTLDLIBS   := $(HOST_LFS_LIBS) $(HOSTLDLIBS)

# Make variables (CC, etc...)
CPP		= $(CC) -E
ifneq ($(LLVM),)
CC		= $(LLVM_PREFIX)clang$(LLVM_SUFFIX)
LD		= $(LLVM_PREFIX)ld.lld$(LLVM_SUFFIX)
AR		= $(LLVM_PREFIX)llvm-ar$(LLVM_SUFFIX)
NM		= $(LLVM_PREFIX)llvm-nm$(LLVM_SUFFIX)
OBJCOPY		= $(LLVM_PREFIX)llvm-objcopy$(LLVM_SUFFIX)
OBJDUMP		= $(LLVM_PREFIX)llvm-objdump$(LLVM_SUFFIX)
READELF		= $(LLVM_PREFIX)llvm-readelf$(LLVM_SUFFIX)
STRIP		= $(LLVM_PREFIX)llvm-strip$(LLVM_SUFFIX)
else
CC		= $(CROSS_COMPILE)gcc
LD		= $(CROSS_COMPILE)ld
AR		= $(CROSS_COMPILE)ar
NM		= $(CROSS_COMPILE)nm
OBJCOPY		= $(CROSS_COMPILE)objcopy
OBJDUMP		= $(CROSS_COMPILE)objdump
READELF		= $(CROSS_COMPILE)readelf
STRIP		= $(CROSS_COMPILE)strip
endif
LEX		= flex
YACC		= bison
AWK		= awk
PERL		= perl
PYTHON3		= python3
BASH		= bash
KGZIP		= gzip


NOSTDINC_FLAGS :=
CFLAGS_MODULE   =
AFLAGS_MODULE   =
LDFLAGS_MODULE  =
CFLAGS_KERNEL	=
AFLAGS_KERNEL	=
LDFLAGS_vmlinux =

# Use USERINCLUDE when you must reference the UAPI directories only.
USERINCLUDE    := \
		-I$(srctree)/arch/$(SRCARCH)/include/uapi \
		-I$(objtree)/arch/$(SRCARCH)/include/generated/uapi \
		-I$(srctree)/include/uapi \
		-I$(objtree)/include/generated/uapi \
                -include $(srctree)/include/linux/kconfig.h

# Use LINUXINCLUDE when you must reference the include/ directory.
# Needed to be compatible with the O= option
LINUXINCLUDE    := \
		-I$(srctree)/arch/$(SRCARCH)/include \
		-I$(objtree)/arch/$(SRCARCH)/include/generated \
		$(if $(building_out_of_srctree),-I$(srctree)/include) \
		-I$(objtree)/include \
		$(USERINCLUDE)

KBUILD_AFLAGS   := -D__ASSEMBLY__ -fno-PIE
KBUILD_CFLAGS   := -Wall -Wundef -Werror=strict-prototypes -Wno-trigraphs \
		   -fno-strict-aliasing -fno-common -fshort-wchar -fno-PIE \
		   -Werror=implicit-function-declaration -Werror=implicit-int \
		   -Werror=return-type -Wno-format-security \
		   -std=gnu11
KBUILD_CPPFLAGS := -D__KERNEL__
KBUILD_AFLAGS_KERNEL :=
KBUILD_CFLAGS_KERNEL :=
KBUILD_AFLAGS_MODULE  := -DMODULE
KBUILD_CFLAGS_MODULE  := -DMODULE
KBUILD_LDFLAGS_MODULE :=
KBUILD_LDFLAGS :=
CLANG_FLAGS :=

export ARCH SRCARCH CONFIG_SHELL BASH HOSTCC KBUILD_HOSTCFLAGS CROSS_COMPILE LD CC HOSTPKG_CONFIG
export CPP AR NM STRIP OBJCOPY OBJDUMP READELF LEX YACC AWK
export PERL PYTHON3 MAKE UTS_MACHINE HOSTCXX
export KGZIP
export KBUILD_HOSTCXXFLAGS KBUILD_HOSTLDFLAGS KBUILD_HOSTLDLIBS LDFLAGS_MODULE
export KBUILD_USERCFLAGS KBUILD_USERLDFLAGS

export KBUILD_CPPFLAGS NOSTDINC_FLAGS LINUXINCLUDE OBJCOPYFLAGS KBUILD_LDFLAGS
export KBUILD_CFLAGS CFLAGS_KERNEL CFLAGS_MODULE
export KBUILD_AFLAGS AFLAGS_KERNEL AFLAGS_MODULE
export KBUILD_AFLAGS_MODULE KBUILD_CFLAGS_MODULE KBUILD_LDFLAGS_MODULE
export KBUILD_AFLAGS_KERNEL KBUILD_CFLAGS_KERNEL

# Files to ignore in find ... statements

export RCS_FIND_IGNORE := \( -name SCCS -o -name BitKeeper -o -name .svn -o    \
			  -name CVS -o -name .pc -o -name .hg -o -name .git \) \
			  -prune -o

# ===========================================================================
# Rules shared between *config targets and build targets

# Basic helpers built in scripts/basic/
PHONY += scripts_basic
scripts_basic:
	$(Q)$(MAKE) $(build)=scripts/basic

PHONY += outputmakefile
outputmakefile:

# The expansion should be delayed until arch/$(SRCARCH)/Makefile is included.
# Some architectures define CROSS_COMPILE in arch/$(SRCARCH)/Makefile.
# CC_VERSION_TEXT is referenced from Kconfig (so it needs export),
# and from include/config/auto.conf.cmd to detect the compiler upgrade.
CC_VERSION_TEXT = $(subst $(pound),,$(shell LC_ALL=C $(CC) --version 2>/dev/null | head -n 1))

ifneq ($(findstring clang,$(CC_VERSION_TEXT)),)
include $(srctree)/scripts/Makefile.clang
endif

# Include this also for config targets because some architectures need
# cc-cross-prefix to determine CROSS_COMPILE.
ifdef need-compiler
include $(srctree)/scripts/Makefile.compiler
endif

ifdef config-build
# ===========================================================================
# *config targets only - make sure prerequisites are updated, and descend
# in scripts/kconfig to make the *config target

# Read arch specific Makefile to set KBUILD_DEFCONFIG as needed.
# KBUILD_DEFCONFIG may point out an alternative default configuration
# used for 'make defconfig'
include $(srctree)/arch/$(SRCARCH)/Makefile
export KBUILD_DEFCONFIG KBUILD_KCONFIG CC_VERSION_TEXT

config: outputmakefile scripts_basic FORCE
	$(Q)$(MAKE) $(build)=scripts/kconfig $@

%config: outputmakefile scripts_basic FORCE
	$(Q)$(MAKE) $(build)=scripts/kconfig $@

else #!config-build
# ===========================================================================
# Build targets only - this includes vmlinux, arch specific targets, clean
# targets and others. In general all targets except *config targets.

PHONY += all
__all: all

KBUILD_MODULES :=
KBUILD_BUILTIN := 1

export KBUILD_MODULES KBUILD_BUILTIN

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
ifdef may-sync-config
# Read in dependencies to all Kconfig* files, make sure to run syncconfig if
# changes are detected. This should be included after arch/$(SRCARCH)/Makefile
# because some architectures define CROSS_COMPILE there.
include include/config/auto.conf.cmd

$(KCONFIG_CONFIG):
	@echo >&2 '***'
	@echo >&2 '*** Configuration file "$@" not found!'
	@echo >&2 '***'
	@echo >&2 '*** Please run some configurator (e.g. "make oldconfig" or'
	@echo >&2 '*** "make menuconfig" or "make xconfig").'
	@echo >&2 '***'
	@/bin/false

# The actual configuration files used during the build are stored in
# include/generated/ and include/config/. Update them if .config is newer than
# include/config/auto.conf (which mirrors .config).
#
# This exploits the 'multi-target pattern rule' trick.
# The syncconfig should be executed only once to make all the targets.
# (Note: use the grouped target '&:' when we bump to GNU Make 4.3)
#
# Do not use $(call cmd,...) here. That would suppress prompts from syncconfig,
# so you cannot notice that Kconfig is waiting for the user input.
%/config/auto.conf %/config/auto.conf.cmd %/generated/autoconf.h: $(KCONFIG_CONFIG)
	$(Q)$(kecho) "  SYNC    $@"
	$(Q)$(MAKE) -f $(srctree)/Makefile syncconfig
else # !may-sync-config
# External modules and some install targets need include/generated/autoconf.h
# and include/config/auto.conf but do not care if they are up-to-date.
# Use auto.conf to trigger the test
PHONY += include/config/auto.conf

include/config/auto.conf:
	$(Q)test -e include/generated/autoconf.h -a -e $@ || (		\
	echo >&2;							\
	echo >&2 "  ERROR: Kernel configuration is invalid.";		\
	echo >&2 "         include/generated/autoconf.h or $@ are missing.";\
	echo >&2 "         Run 'make oldconfig && make prepare' on kernel src to fix it.";	\
	echo >&2 ;							\
	/bin/false)

endif # may-sync-config
endif # need-config

KBUILD_CFLAGS	+= -fno-delete-null-pointer-checks
KBUILD_CFLAGS	+= $(call cc-disable-warning,frame-address,)
KBUILD_CFLAGS	+= $(call cc-disable-warning, format-truncation)
KBUILD_CFLAGS	+= $(call cc-disable-warning, format-overflow)
KBUILD_CFLAGS	+= $(call cc-disable-warning, address-of-packed-member)

ifdef CONFIG_CC_OPTIMIZE_FOR_PERFORMANCE
KBUILD_CFLAGS += -O2
else ifdef CONFIG_CC_OPTIMIZE_FOR_PERFORMANCE_O3
KBUILD_CFLAGS += -O3
else ifdef CONFIG_CC_OPTIMIZE_FOR_SIZE
KBUILD_CFLAGS += -Os
endif

# Tell gcc to never replace conditional load with a non-conditional one


ifneq ($(CONFIG_FRAME_WARN),0)
KBUILD_CFLAGS += -Wframe-larger-than=$(CONFIG_FRAME_WARN)
endif

stackp-flags-y                                    := -fno-stack-protector
stackp-flags-$(CONFIG_STACKPROTECTOR)             := -fstack-protector
stackp-flags-$(CONFIG_STACKPROTECTOR_STRONG)      := -fstack-protector-strong

KBUILD_CFLAGS += $(stackp-flags-y)

KBUILD_CFLAGS-$(CONFIG_WERROR) += -Werror
KBUILD_CFLAGS-$(CONFIG_CC_NO_ARRAY_BOUNDS) += -Wno-array-bounds
KBUILD_CFLAGS += $(KBUILD_CFLAGS-y) $(CONFIG_CC_IMPLICIT_FALLTHROUGH)

KBUILD_CPPFLAGS += -Qunused-arguments
# The kernel builds with '-std=gnu11' so use of GNU extensions is acceptable.
KBUILD_CFLAGS += -Wno-gnu

# These warnings generated too much noise in a regular build.
# Use make W=1 to enable them (see scripts/Makefile.extrawarn)
KBUILD_CFLAGS += $(call cc-disable-warning, unused-but-set-variable)
KBUILD_CFLAGS += $(call cc-disable-warning, unused-const-variable)

# These result in bogus false positives
KBUILD_CFLAGS += $(call cc-disable-warning, dangling-pointer)

KBUILD_CFLAGS	+= -fomit-frame-pointer



# While VLAs have been removed, GCC produces unreachable stack probes
# for the randomize_kstack_offset feature. Disable it for all compilers.
KBUILD_CFLAGS	+= $(call cc-option, -fno-stack-clash-protection)






CC_FLAGS_LTO	:= -flto
CC_FLAGS_LTO	+= -fvisibility=hidden

KBUILD_LDFLAGS += -mllvm -import-instr-limit=5

KBUILD_CFLAGS	+= -fno-lto $(CC_FLAGS_LTO)
KBUILD_AFLAGS	+= -fno-lto
export CC_FLAGS_LTO



# arch Makefile may override CC so keep this after arch Makefile is included
NOSTDINC_FLAGS += -nostdinc

# warn about C99 declaration after statement
KBUILD_CFLAGS += -Wdeclaration-after-statement

# Variable Length Arrays (VLAs) should not be used anywhere in the kernel
KBUILD_CFLAGS += -Wvla

# disable pointer signed / unsigned warnings in gcc 4.0
KBUILD_CFLAGS += -Wno-pointer-sign

# In order to make sure new function cast mismatches are not introduced
# in the kernel (to avoid tripping CFI checking), the kernel should be
# globally built with -Wcast-function-type.
KBUILD_CFLAGS += $(call cc-option, -Wcast-function-type)

# disable stringop warnings in gcc 8+
KBUILD_CFLAGS += $(call cc-disable-warning, stringop-truncation)

# We'll want to enable this eventually, but it's not going away for 5.7 at least
KBUILD_CFLAGS += $(call cc-disable-warning, stringop-overflow)

# Another good warning that we'll want to enable eventually
KBUILD_CFLAGS += $(call cc-disable-warning, restrict)

# Enabled with W=2, disabled by default as noisy


# disable invalid "can't wrap" optimizations for signed / pointers
KBUILD_CFLAGS	+= -fno-strict-overflow

# Make sure -fstack-check isn't enabled (like gentoo apparently did)
KBUILD_CFLAGS  += -fno-stack-check

# conserve stack if available

# Prohibit date/time macros, which would make the build non-deterministic
KBUILD_CFLAGS   += -Werror=date-time

# enforce correct pointer usage
KBUILD_CFLAGS   += $(call cc-option,-Werror=incompatible-pointer-types)

# Require designated initializers for all marked structures
KBUILD_CFLAGS   += $(call cc-option,-Werror=designated-init)

# change __FILE__ to the relative path from the srctree
KBUILD_CPPFLAGS += $(call cc-option,-fmacro-prefix-map=$(srctree)/=)

# include additional Makefiles when needed
include-y			:= scripts/Makefile.extrawarn

include $(addprefix $(srctree)/, $(include-y))


# Add user supplied CPPFLAGS, AFLAGS and CFLAGS as the last assignments
KBUILD_CPPFLAGS += $(KCPPFLAGS)
KBUILD_AFLAGS   += $(KAFLAGS)
KBUILD_CFLAGS   += $(KCFLAGS)

LDFLAGS_vmlinux += --build-id=sha1



# We never want expected sections to be placed heuristically by the
# linker. All sections should be explicitly named in the linker script.
ifdef CONFIG_LD_ORPHAN_WARN
LDFLAGS_vmlinux += --orphan-handling=warn
endif

# Align the bit size of userspace programs with the kernel
KBUILD_USERCFLAGS  += $(filter -m32 -m64 --target=%, $(KBUILD_CFLAGS))
KBUILD_USERLDFLAGS += $(filter -m32 -m64 --target=%, $(KBUILD_CFLAGS))


# Default kernel image to build when no specific target is given.
# KBUILD_IMAGE may be overruled on the command line or
# set in the environment
# Also any assignments in arch/$(ARCH)/Makefile take precedence over
# this default value
export KBUILD_IMAGE ?= vmlinux



PHONY += prepare0

export MODORDER := modules.order
export MODULES_NSDEPS := modules.nsdeps

core-y			+= kernel/ mm/ fs/ security/

vmlinux-dirs	:= $(patsubst %/,%,$(filter %/, \
		     $(core-y) $(core-m) $(drivers-y) $(drivers-m) \
		     $(libs-y) $(libs-m)))

vmlinux-alldirs	:= $(sort $(vmlinux-dirs) \
		     $(patsubst %/,%,$(filter %/, $(core-) \
			$(drivers-) $(libs-))))

subdir-modorder := $(addsuffix modules.order,$(filter %/, \
			$(core-y) $(core-m) $(libs-y) $(libs-m) \
			$(drivers-y) $(drivers-m)))

build-dirs	:= $(vmlinux-dirs)
clean-dirs	:= $(vmlinux-alldirs)

# Externally visible symbols (used by link-vmlinux.sh)
KBUILD_VMLINUX_OBJS := $(head-y) $(patsubst %/,%/built-in.a, $(core-y))
KBUILD_VMLINUX_OBJS += $(addsuffix built-in.a, $(filter %/, $(libs-y)))
KBUILD_VMLINUX_LIBS := $(patsubst %/,%/lib.a, $(libs-y))
KBUILD_VMLINUX_OBJS += $(patsubst %/,%/built-in.a, $(drivers-y))

export KBUILD_VMLINUX_OBJS KBUILD_VMLINUX_LIBS
export KBUILD_LDS          := arch/$(SRCARCH)/kernel/vmlinux.lds

vmlinux-deps := $(KBUILD_LDS) $(KBUILD_VMLINUX_OBJS) $(KBUILD_VMLINUX_LIBS)


ARCH_POSTLINK := $(wildcard $(srctree)/arch/$(SRCARCH)/Makefile.postlink)

# Final link of vmlinux with optional arch pass after final link
cmd_link-vmlinux =                                                 \
	$(CONFIG_SHELL) $< "$(LD)" "$(KBUILD_LDFLAGS)" "$(LDFLAGS_vmlinux)";    \
	$(if $(ARCH_POSTLINK), $(MAKE) -f $(ARCH_POSTLINK) $@, true)

vmlinux: scripts/link-vmlinux.sh $(vmlinux-deps) FORCE
	+$(call if_changed_dep,link-vmlinux)

targets := vmlinux

# The actual objects are generated when descending,
# make sure no implicit rule kicks in
$(sort $(vmlinux-deps) $(subdir-modorder)): descend ;

filechk_kernel.release = \
	echo "$(KERNELVERSION)$$($(CONFIG_SHELL) $(srctree)/scripts/setlocalversion $(srctree))"

# Store (new) KERNELRELEASE string in include/config/kernel.release
include/config/kernel.release: FORCE
	$(call filechk,kernel.release)

# Additional helpers built in scripts/
# Carefully list dependencies so we do not try to build scripts twice
# in parallel
PHONY += scripts
scripts: scripts_basic
	$(Q)$(MAKE) $(build)=$(@)

PHONY += prepare archprepare

archprepare: outputmakefile archheaders archscripts scripts include/config/kernel.release \
	asm-generic $(version_h) include/generated/utsrelease.h \
	include/generated/autoconf.h

prepare0: archprepare
	$(Q)$(MAKE) $(build)=scripts/mod
	$(Q)$(MAKE) $(build)=.

# All the preparing..
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

# Generate some files
# ---------------------------------------------------------------------------

# KERNELRELEASE can change from a few different places, meaning version.h
# needs to be updated, so this check is forced on all builds

uts_len := 64
define filechk_utsrelease.h
	if [ `echo -n "$(KERNELRELEASE)" | wc -c ` -gt $(uts_len) ]; then \
	  echo '"$(KERNELRELEASE)" exceeds $(uts_len) characters' >&2;    \
	  exit 1;                                                         \
	fi;                                                               \
	echo \#define UTS_RELEASE \"$(KERNELRELEASE)\"
endef

define filechk_version.h
	if [ $(SUBLEVEL) -gt 255 ]; then                                 \
		echo \#define LINUX_VERSION_CODE $(shell                 \
		expr $(VERSION) \* 65536 + $(PATCHLEVEL) \* 256 + 255); \
	else                                                             \
		echo \#define LINUX_VERSION_CODE $(shell                 \
		expr $(VERSION) \* 65536 + $(PATCHLEVEL) \* 256 + $(SUBLEVEL)); \
	fi;                                                              \
	echo '#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) +  \
	((c) > 255 ? 255 : (c)))';                                       \
	echo \#define LINUX_VERSION_MAJOR $(VERSION);                    \
	echo \#define LINUX_VERSION_PATCHLEVEL $(PATCHLEVEL);            \
	echo \#define LINUX_VERSION_SUBLEVEL $(SUBLEVEL)
endef

$(version_h): PATCHLEVEL := $(or $(PATCHLEVEL), 0)
$(version_h): SUBLEVEL := $(or $(SUBLEVEL), 0)
$(version_h): FORCE
	$(call filechk,version.h)

include/generated/utsrelease.h: include/config/kernel.release FORCE
	$(call filechk,utsrelease.h)





# ---------------------------------------------------------------------------
# Tools





# ---------------------------------------------------------------------------
# Modules


# Directories & files removed with 'make clean'
CLEAN_FILES += include/ksym vmlinux.symvers .thinlto-cache

# Directories & files removed with 'make mrproper'
MRPROPER_FILES += include/config include/generated          \
		  arch/$(SRCARCH)/include/generated \
		  .config .config.old .version

# clean - Delete most, but leave enough to build external modules
#
clean: rm-files := $(CLEAN_FILES)

PHONY += archclean vmlinuxclean

vmlinuxclean:
	$(Q)$(CONFIG_SHELL) $(srctree)/scripts/link-vmlinux.sh clean
	$(Q)$(if $(ARCH_POSTLINK), $(MAKE) -f $(ARCH_POSTLINK) clean)

clean: archclean vmlinuxclean

# mrproper - Delete all generated files, including .config
#
mrproper: rm-files := $(wildcard $(MRPROPER_FILES))
mrproper-dirs      := $(addprefix _mrproper_,scripts)

PHONY += $(mrproper-dirs) mrproper
$(mrproper-dirs):
	$(Q)$(MAKE) $(clean)=$(patsubst _mrproper_%,%,$@)

mrproper: clean $(mrproper-dirs)
	$(call cmd,rmfiles)

# Handle descending into subdirectories listed in $(build-dirs)
# Preset locale variables to speed up the build process. Limit locale
# tweaks to this spot to avoid wrong language settings when running
# make menuconfig etc.
# Error messages still appears in the original language
PHONY += descend $(build-dirs)
descend: $(build-dirs)
$(build-dirs): prepare
	$(Q)$(MAKE) $(build)=$@ need-builtin=1 need-modorder=1

clean-dirs := $(addprefix _clean_, $(clean-dirs))
PHONY += $(clean-dirs) clean
$(clean-dirs):
	$(Q)$(MAKE) $(clean)=$(patsubst _clean_%,%,$@)

clean: $(clean-dirs)
	$(call cmd,rmfiles)
	@find . $(RCS_FIND_IGNORE) \
		\( -name '*.[aios]' -o -name '.*.cmd' \
		-o -name '*.dwo' -o -name '*.lst' \
		-o -name '.*.d' -o -name '.*.tmp' \
		-o -name 'modules.order' \
		-o -name '.tmp_*' \
		-o -name '*.ll' \) -type f -print | xargs rm -f

quiet_cmd_rmfiles = $(if $(wildcard $(rm-files)),CLEAN   $(wildcard $(rm-files)))
      cmd_rmfiles = rm -rf $(rm-files)

# read saved command lines for existing targets
existing-targets := $(wildcard $(sort $(targets)))

-include $(foreach f,$(existing-targets),$(dir $(f)).$(notdir $(f)).cmd)

endif # config-build
endif # mixed-build
endif # need-sub-make

PHONY += FORCE
FORCE:

.PHONY: $(PHONY)
