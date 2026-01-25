
#include <linux/build_bug.h>

#include "boot.h"
#include "string.h"

struct boot_params boot_params __attribute__((aligned(16)));

struct port_io_ops pio_ops;

char *HEAP = _end;
char *heap_end = _end;

/* copy_boot_params, keyboard_init, query_ist, init_heap inlined into main */
/* set_bios_mode removed - was empty stub */

void main(void)
{
	struct biosregs ireg, oreg;

	init_default_io_ops();

	/* copy_boot_params inlined */
	{
		struct old_cmdline {
			u16 cl_magic;
			u16 cl_offset;
		};
		const struct old_cmdline *const oldcmd =
			absolute_pointer(OLD_CL_ADDRESS);
		BUILD_BUG_ON(sizeof(boot_params) != 4096);
		memcpy(&boot_params.hdr, &hdr, sizeof(hdr));
		if (!boot_params.hdr.cmd_line_ptr &&
		    oldcmd->cl_magic == OLD_CL_MAGIC) {
			u16 cmdline_seg;
			if (oldcmd->cl_offset < boot_params.hdr.setup_move_size)
				cmdline_seg = ds();
			else
				cmdline_seg = 0x9000;
			boot_params.hdr.cmd_line_ptr =
				(cmdline_seg << 4) + oldcmd->cl_offset;
		}
	}

	/* console_init removed - was empty stub (no serial console) */
	if (cmdline_find_option_bool("debug"))
		puts("early console in setup code\n");

	/* init_heap inlined */
	if (boot_params.hdr.loadflags & CAN_USE_HEAP) {
		char *stack_end;
		asm("leal %P1(%%esp),%0" : "=r"(stack_end) : "i"(-STACK_SIZE));
		heap_end =
			(char *)((size_t)boot_params.hdr.heap_end_ptr + 0x200);
		if (heap_end > stack_end)
			heap_end = stack_end;
	} else {
		puts("WARNING: Ancient bootloader, some functionality may be limited!\n");
	}

	if (validate_cpu()) {
		puts("Unable to boot - please use a kernel appropriate for your CPU.\n");
		die();
	}

	/* set_bios_mode removed - was empty stub */

	detect_memory();

	/* keyboard_init inlined */
	initregs(&ireg);
	ireg.ah = 0x02;
	intcall(0x16, &ireg, &oreg);
	boot_params.kbd_status = oreg.al;
	ireg.ax = 0x0305;
	intcall(0x16, &ireg, NULL);

	/* query_ist inlined */
	if (cpu.level >= 6) {
		initregs(&ireg);
		ireg.ax = 0xe980;
		ireg.edx = 0x47534943;
		intcall(0x15, &ireg, &oreg);
		boot_params.ist_info.signature = oreg.eax;
		boot_params.ist_info.command = oreg.ebx;
		boot_params.ist_info.event = oreg.ecx;
		boot_params.ist_info.perf_level = oreg.edx;
	}

	set_video();

	go_to_protected_mode();
}
