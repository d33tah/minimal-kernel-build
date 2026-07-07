# SPDX-License-Identifier: GPL-2.0
#
# Makefile for some libs needed in the kernel.
#

ccflags-remove-$(CONFIG_FUNCTION_TRACER) += $(CC_FLAGS_FTRACE)

# These files are disabled because they produce lots of non-interesting and/or
# flaky coverage that is not a function of syscall inputs. For example,
# rbtree can be global and individual rotations don't correlate with inputs.
KCOV_INSTRUMENT_string.o := n
KCOV_INSTRUMENT_rbtree.o := n

# string.o implements standard library functions like memset/memcpy etc.
# Use -ffreestanding to ensure that the compiler does not try to "optimize"
# them into calls to themselves.
CFLAGS_string.o := -ffreestanding

# Early boot use of cmdline, don't instrument it
ifdef CONFIG_AMD_MEM_ENCRYPT
KASAN_SANITIZE_string.o := n

CFLAGS_string.o += -fno-stack-protector
endif

lib-y := ctype.o string.o vsprintf.o \
	 rbtree.o radix-tree.o xarray.o \
	 idr.o extable.o irq_regs.o \
	 ratelimit.o \
	 siphash.o dec_and_lock.o
	 # argv_split.o - Removed: not used
	 # buildid.o - Removed: Not needed (stub functions)


lib-y	+= kobject.o
obj-y	+= lockref.o

obj-y += sort.o debug_locks.o \
	 bust_spinlocks.o kasprintf.o bitmap.o \
	 iov_iter.o \
	 find_bit.o
	 # parser.o removed - not used
	 # usercopy.o removed - _copy_from_user/_copy_to_user now header inlines
	 # (INLINE_COPY_FROM_USER/TO_USER defined in asm/uaccess.h)
obj-y += string_helpers.o
obj-y += hexdump.o
obj-y += kstrtox.o


ifeq ($(CONFIG_DEBUG_KOBJECT),y)
CFLAGS_kobject.o += -DDEBUG
endif


obj-y += math/ crypto/

# iomap.o removed - just a comment
# obj-$(CONFIG_GENERIC_PCI_IOMAP) += pci_iomap.o  # deleted
# devres.o removed - just comments

# lib-y += logic_pio.o  # deleted





lib-$(CONFIG_DECOMPRESS_XZ) += decompress_unxz.o


obj-$(CONFIG_OF_RECONFIG_NOTIFIER_ERROR_INJECT) += \
	of-reconfig-notifier-error-inject.o


# obj-$(CONFIG_HAVE_ARCH_TRACEHOOK) += syscall.o  # deleted












obj-$(CONFIG_GENERIC_STRNLEN_USER) += strnlen_user.o




# stackdepot.c should not be instrumented or call instrumented functions.


libfdt_files = fdt.o fdt_ro.o fdt_wip.o fdt_rw.o fdt_sw.o fdt_strerror.o \
	       fdt_empty_tree.o fdt_addresses.o
$(foreach file, $(libfdt_files), \
	$(eval CFLAGS_$(file) = -I $(srctree)/scripts/dtc/libfdt))
lib-$(CONFIG_LIBFDT) += $(libfdt_files)


$(obj)/bootconfig-data.o: $(obj)/default.bconf

targets += default.bconf
filechk_defbconf = cat $(or $(real-prereqs), /dev/null)
$(obj)/default.bconf: $(CONFIG_BOOT_CONFIG_EMBED_FILE) FORCE
	$(call filechk,defbconf)





#
# Build a fast OID lookip registry from include/linux/oid_registry.h
#

$(obj)/oid_registry.o: $(obj)/oid_registry_data.c

$(obj)/oid_registry_data.c: $(srctree)/include/linux/oid_registry.h \
			    $(src)/build_OID_registry
	$(call cmd,build_OID_registry)

quiet_cmd_build_OID_registry = GEN     $@
      cmd_build_OID_registry = perl $(srctree)/$(src)/build_OID_registry $< $@

clean-files	+= oid_registry_data.c

ifneq ($(CONFIG_UBSAN_TRAP),y)
endif



# GCC library routines

# KUnit tests


# FORTIFY_SOURCE compile-time behavior tests
TEST_FORTIFY_SRCS = $(wildcard $(srctree)/$(src)/test_fortify/*-*.c)
TEST_FORTIFY_LOGS = $(patsubst $(srctree)/$(src)/%.c, %.log, $(TEST_FORTIFY_SRCS))
TEST_FORTIFY_LOG = test_fortify.log

quiet_cmd_test_fortify = TEST    $@
      cmd_test_fortify = $(CONFIG_SHELL) $(srctree)/scripts/test_fortify.sh \
			$< $@ "$(NM)" $(CC) $(c_flags) \
			$(call cc-disable-warning,fortify-source) \
			-DKBUILD_EXTRA_WARN1

targets += $(TEST_FORTIFY_LOGS)
clean-files += $(TEST_FORTIFY_LOGS)
clean-files += $(addsuffix .o, $(TEST_FORTIFY_LOGS))
$(obj)/test_fortify/%.log: $(src)/test_fortify/%.c \
			   $(src)/test_fortify/test_fortify.h \
			   $(srctree)/include/linux/fortify-string.h \
			   $(srctree)/scripts/test_fortify.sh \
			   FORCE
	$(call if_changed,test_fortify)

quiet_cmd_gen_fortify_log = GEN     $@
      cmd_gen_fortify_log = cat </dev/null $(filter-out FORCE,$^) 2>/dev/null > $@ || true

targets += $(TEST_FORTIFY_LOG)
clean-files += $(TEST_FORTIFY_LOG)
$(obj)/$(TEST_FORTIFY_LOG): $(addprefix $(obj)/, $(TEST_FORTIFY_LOGS)) FORCE
	$(call if_changed,gen_fortify_log)

# Fake dependency to trigger the fortify tests.
ifeq ($(CONFIG_FORTIFY_SOURCE),y)
$(obj)/string.o: $(obj)/$(TEST_FORTIFY_LOG)
endif
