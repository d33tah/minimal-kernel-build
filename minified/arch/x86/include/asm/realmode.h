 
#ifndef _ARCH_X86_REALMODE_H
#define _ARCH_X86_REALMODE_H

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <asm/io.h>

struct real_mode_header {
	u32	text_start;
	u32	ro_end;
	 
	u32	trampoline_start;
	u32	trampoline_header;
	 
	u32	machine_real_restart_asm;
};

struct trampoline_header {
	u32 start;
	u16 gdt_pad;
	u16 gdt_limit;
	u32 gdt_base;
};

extern struct real_mode_header *real_mode_header;

extern unsigned char boot_gdt[];

void reserve_real_mode(void);

#endif  

#endif  
