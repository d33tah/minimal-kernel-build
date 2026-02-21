# SPDX-License-Identifier: GPL-2.0
# VDSO disabled - only build stub kernel objects

# Sanitizer runtimes are unavailable and cannot be linked here.
KASAN_SANITIZE			:= n
UBSAN_SANITIZE			:= n
KCSAN_SANITIZE			:= n
OBJECT_FILES_NON_STANDARD	:= y
KCOV_INSTRUMENT		:= n

# files to link into kernel (stubs only)
obj-y				+= vma.o extable.o
OBJECT_FILES_NON_STANDARD_vma.o	:= n

GCOV_PROFILE := n
