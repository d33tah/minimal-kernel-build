; Real-mode setup for the hello kernel: the part of a bzImage that a boot
; loader (QEMU -kernel, v86, SYSLINUX, GRUB) loads below 1 MiB.
;
; Layout follows Documentation/x86/boot.rst: a 512-byte "boot sector" whose
; tail holds the first header fields, then the "HdrS" header at 0x202 and
; the 16-bit entry point at 0x200. setup_sects and syssize are left zero
; here and filled in by tools/build.c.
;
; Entry contract (boot.rst, "Running the Kernel"): CS = base+0x20, IP = 0,
; DS = ES = SS = base, interrupts may be on. We enable A20, load a flat
; GDT, switch to protected mode and jump to code32_start with
; ESI = linear address of boot_params (the start of this image in memory).

	bits	16
	org	0

; Two layouts, chosen by the Makefile (nasm -D):
;   high (bzImage):     32-bit part at 1 MiB, LOADED_HIGH set
;   low  (bzImage.low): 32-bit part at 64 KiB, LOADED_HIGH clear (the
;                       zImage layout; setup goes to 0x90000), fits in 1 MiB
%ifndef KERNEL_BASE
%define KERNEL_BASE 0x100000
%endif
%if KERNEL_BASE >= 0x100000
%define LOADFLAGS 0x01
%else
%define LOADFLAGS 0x00
%endif

; ---------------------------------------------------------------------------
; 0x000: legacy boot sector. Booting the image straight from a floppy is not
; supported, so it only says so and stops.
bootsect:
	mov	ax, 0x07c0
	mov	ds, ax
	mov	si, bootmsg
.print:	lodsb
	test	al, al
	jz	.stop
	mov	ah, 0x0e
	mov	bx, 7
	int	0x10
	jmp	.print
.stop:	cli
	hlt
	jmp	.stop
bootmsg	db	"Use a boot loader.", 13, 10, 0

	times	0x1f1 - ($ - $$) db 0

; ---------------------------------------------------------------------------
; 0x1f1: setup header, part 1 (offsets are fixed by the protocol).
setup_sects	db	0		; 0x1f1  filled in by build
root_flags	dw	0		; 0x1f2
syssize		dd	0		; 0x1f4  filled in by build (16-byte units)
ram_size	dw	0		; 0x1f8  obsolete
vid_mode	dw	0xffff		; 0x1fa  "normal"
root_dev	dw	0		; 0x1fc
boot_flag	dw	0xaa55		; 0x1fe

; 0x200: the real-mode entry point, then the rest of the header.
	jmp	short start_of_setup	; 0x200
	db	"HdrS"			; 0x202  header magic
	dw	0x020a			; 0x206  boot protocol 2.10
realmode_swtch	dd	0		; 0x208
start_sys_seg	dw	0x1000		; 0x20c  obsolete
kernel_version	dw	version_str - 0x200	; 0x20e
type_of_loader	db	0		; 0x210  written by the loader
loadflags	db	LOADFLAGS	; 0x211  bit 0 LOADED_HIGH: 32-bit part at 1 MiB
setup_move_size	dw	0x8000		; 0x212
code32_start	dd	KERNEL_BASE	; 0x214  32-bit entry point
ramdisk_image	dd	0		; 0x218
ramdisk_size	dd	0		; 0x21c
bootsect_kludge	dd	0		; 0x220
heap_end_ptr	dw	0		; 0x224
ext_loader_ver	db	0		; 0x226
ext_loader_type	db	0		; 0x227
cmd_line_ptr	dd	0		; 0x228
initrd_addr_max	dd	0x7fffffff	; 0x22c
kernel_alignment dd	0x1000		; 0x230
relocatable	db	0		; 0x234  must be loaded at code32_start
min_alignment	db	12		; 0x235
xloadflags	dw	0		; 0x236
cmdline_size	dd	255		; 0x238
hw_subarch	dd	0		; 0x23c
hw_subarch_data	dq	0		; 0x240
payload_offset	dd	0		; 0x248
payload_length	dd	0		; 0x24c
setup_data	dq	0		; 0x250
pref_address	dq	KERNEL_BASE	; 0x258
init_size	dd	0		; 0x260  filled in by build: code, data, bss, stack
handover_offset	dd	0		; 0x264

version_str	db	"hello 1.0 (not Linux)", 0

; ---------------------------------------------------------------------------
; Labels are offsets from the start of the image, which is DS:0, so all data
; is addressed through DS. CS is DS+0x20 and is only used for relative jumps.
start_of_setup:
	cli
	cld

	; A20, simplest of upstream's methods: ask the BIOS, then use the
	; "fast A20" bit of system control port A.
	mov	ax, 0x2401
	int	0x15
	in	al, 0x92
	or	al, 0x02
	and	al, 0xfe		; bit 0 would reset the machine
	out	0x92, al

	; ESI = linear address of this image (= boot_params). Relocate the GDT
	; pointer and the far-jump target by it.
	xor	esi, esi
	mov	si, ds
	shl	esi, 4
	add	[gdt_ptr + 2], esi
	add	[pm_target], esi

	lgdt	[gdt_ptr]
	mov	eax, cr0
	or	al, 1			; PE
	mov	cr0, eax
	jmp	far dword [pm_target]	; loads CS = __BOOT_CS, enters 32-bit code

	bits	32
in_pm32:
	mov	eax, 0x18		; __BOOT_DS
	mov	ds, eax
	mov	es, eax
	mov	fs, eax
	mov	gs, eax
	mov	ss, eax
	jmp	[esi + 0x214]		; code32_start, as the loader left it

	align	8
gdt:	dq	0
	dq	0
	dq	0x00cf9a000000ffff	; 0x10 __BOOT_CS: flat 4 GiB, code, 32-bit
	dq	0x00cf92000000ffff	; 0x18 __BOOT_DS: flat 4 GiB, data
gdt_end:

gdt_ptr:	dw	gdt_end - gdt - 1
		dd	gdt		; + image base at run time
pm_target:	dd	in_pm32		; + image base at run time
		dw	0x10
