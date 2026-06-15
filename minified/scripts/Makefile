# SPDX-License-Identifier: GPL-2.0
###
# scripts contains sources for various helper programs used throughout
# the kernel for the build process.

hostprogs-always-$(CONFIG_BUILD_BIN2C)			+= bin2c
hostprogs-always-$(CONFIG_KALLSYMS)			+= kallsyms
hostprogs-always-$(BUILD_C_RECORDMCOUNT)		+= recordmcount
hostprogs-always-$(CONFIG_ASN1)				+= asn1_compiler
hostprogs-always-$(CONFIG_MODULE_SIG_FORMAT)		+= sign-file
hostprogs-always-$(CONFIG_SYSTEM_EXTRA_CERTIFICATE)	+= insert-sys-cert

HOSTCFLAGS_asn1_compiler.o = -I$(srctree)/include
HOSTCFLAGS_sign-file.o = $(shell $(HOSTPKG_CONFIG) --cflags libcrypto 2> /dev/null)
HOSTLDLIBS_sign-file = $(shell $(HOSTPKG_CONFIG) --libs libcrypto 2> /dev/null || echo -lcrypto)

# The following programs are only built on demand
hostprogs += unifdef

# The module linker script is preprocessed on demand
targets += module.lds

# Let clean descend into subdirs
subdir-	+= basic dtc kconfig mod
