
#include "boot.h"

#define SMAP 0x534d4150

void detect_memory(void)
{
	int count = 0;
	struct biosregs ireg, oreg;
	struct boot_e820_entry *desc = boot_params.e820_table;
	static struct boot_e820_entry buf;

	/* detect_memory_e820 */
	initregs(&ireg);
	ireg.ax = 0xe820;
	ireg.cx = sizeof(buf);
	ireg.edx = SMAP;
	ireg.di = (size_t)&buf;
	do {
		intcall(0x15, &ireg, &oreg);
		ireg.ebx = oreg.ebx;
		if (oreg.eflags & X86_EFLAGS_CF)
			break;
		if (oreg.eax != SMAP) {
			count = 0;
			break;
		}
		*desc++ = buf;
		count++;
	} while (ireg.ebx && count < ARRAY_SIZE(boot_params.e820_table));
	boot_params.e820_entries = count;
}
