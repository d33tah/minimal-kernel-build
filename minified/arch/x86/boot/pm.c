

#include "boot.h"
#include <asm/segment.h>

struct gdt_ptr {
	u16 len;
	u32 ptr;
} __attribute__((packed));

void go_to_protected_mode(void)
{
	static const u64 boot_gdt[] __attribute__((aligned(16))) = {
		[GDT_ENTRY_BOOT_CS] = GDT_ENTRY(0xc09b, 0, 0xfffff),
		[GDT_ENTRY_BOOT_DS] = GDT_ENTRY(0xc093, 0, 0xfffff),
		[GDT_ENTRY_BOOT_TSS] = GDT_ENTRY(0x0089, 4096, 103),
	};
	static struct gdt_ptr gdt;
	static const struct gdt_ptr null_idt = { 0, 0 };

	if (boot_params.hdr.realmode_swtch) {
		asm volatile("lcallw *%0"
			     :
			     : "m"(boot_params.hdr.realmode_swtch)
			     : "eax", "ebx", "ecx", "edx");
	} else {
		asm volatile("cli");
		outb(0x80, 0x70);
		io_delay();
	}

	if (enable_a20()) {
		puts("A20 gate not responding, unable to boot...\n");
		die();
	}

	outb(0, 0xf0);
	io_delay();
	outb(0, 0xf1);
	io_delay();

	outb(0xff, 0xa1);
	io_delay();
	outb(0xfb, 0x21);
	io_delay();

	asm volatile("lidtl %0" : : "m"(null_idt));

	gdt.len = sizeof(boot_gdt) - 1;
	gdt.ptr = (u32)&boot_gdt + (ds() << 4);
	asm volatile("lgdtl %0" : : "m"(gdt));

	protected_mode_jump(boot_params.hdr.code32_start,
			    (u32)&boot_params + (ds() << 4));
}
