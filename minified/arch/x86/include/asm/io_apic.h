/* Minimal io_apic.h - IO APIC disabled */
#ifndef _ASM_X86_IO_APIC_H
#define _ASM_X86_IO_APIC_H

#include <asm/x86_init.h>

/* IO APIC stubs - disabled */
#define setup_ioapic_ids_from_mpc x86_init_noop
/* native_io_apic_read, native_restore_boot_irq_mode removed - x86_apic_ops removed */

#endif
