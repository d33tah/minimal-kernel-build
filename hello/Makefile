# Toy "hello world" kernel in the bzImage format. See README.md.
#
#   make LLVM=1    build bzImage and bzImage.low with clang/ld.lld (as CI)
#   make test      boot both in qemu and check the VGA screen (vmtest.tcl)
#   make clean
#
# bzImage      32-bit part at 1 MiB (LOADED_HIGH). Loads everywhere,
#              including v86, which always puts the 32-bit part at 1 MiB.
# bzImage.low  32-bit part at 64 KiB (LOADED_HIGH clear, the zImage layout).
#              Runs in 1 MiB of RAM; needs a loader that honours loadflags
#              (qemu -kernel, SYSLINUX, GRUB do; v86 does not).

ifeq ($(LLVM),1)
CC      := clang
LD      := ld.lld
OBJCOPY := llvm-objcopy
NM      := llvm-nm
HOSTCC  := clang
else
CC      ?= cc
LD      := ld
OBJCOPY := objcopy
NM      := nm
HOSTCC  := cc
endif
NASM    := nasm

CFLAGS  := -m32 -march=i386 -std=gnu11 -Os -Wall -Wextra -Werror \
           -ffreestanding -nostdlib -fno-pic -fno-stack-protector \
           -fno-asynchronous-unwind-tables -mno-sse -mno-mmx
LDFLAGS := -m elf_i386 -nostdlib --no-dynamic-linker -static

BASE_high := 0x100000
BASE_low  := 0x10000

all: bzImage bzImage.low

boot/hello.o: boot/hello.c
	$(CC) $(CFLAGS) -c -o $@ $<

boot/head.o: boot/head.asm
	$(NASM) -f elf32 -o $@ $<

boot/%/setup.bin: boot/setup.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -DKERNEL_BASE=$(BASE_$*) -o $@ $<

boot/%/kernel.elf: boot/head.o boot/hello.o boot/hello.lds
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) --defsym=KERNEL_BASE=$(BASE_$*) -T boot/hello.lds \
		-o $@ boot/head.o boot/hello.o

boot/%/kernel.bin: boot/%/kernel.elf
	$(OBJCOPY) -O binary $< $@

tools/build: tools/build.c
	$(HOSTCC) -O2 -Wall -Wextra -Werror -o $@ $<

bzImage: tools/build boot/high/setup.bin boot/high/kernel.bin
	tools/build boot/high/setup.bin boot/high/kernel.bin \
		0x$$($(NM) boot/high/kernel.elf | sed -n "s/ [A-Za-z] _image_size$$//p") $@

bzImage.low: tools/build boot/low/setup.bin boot/low/kernel.bin
	tools/build boot/low/setup.bin boot/low/kernel.bin \
		0x$$($(NM) boot/low/kernel.elf | sed -n "s/ [A-Za-z] _image_size$$//p") $@

test: bzImage bzImage.low
	TERM=$${TERM:-xterm} ./vmtest.tcl bzImage
	TERM=$${TERM:-xterm} ./vmtest.tcl bzImage.low 1M

clean:
	rm -rf bzImage bzImage.low boot/*.o boot/high boot/low tools/build

.PHONY: all test clean
.SECONDARY:
