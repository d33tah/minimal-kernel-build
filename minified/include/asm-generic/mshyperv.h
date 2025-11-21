#ifndef _ASM_GENERIC_MSHYPERV_H
#define _ASM_GENERIC_MSHYPERV_H

/* Minimal stub - hypervisor support not needed for basic kernel */

static inline void hv_setup_vmbus_handler(void (*handler)(void)) { }
static inline void hv_remove_vmbus_handler(void) { }
static inline void hv_setup_kexec_handler(void (*handler)(void)) { }
static inline void hv_remove_kexec_handler(void) { }
static inline void hv_setup_crash_handler(void (*handler)(struct pt_regs *regs)) { }
static inline void hv_remove_crash_handler(void) { }
static inline bool hv_is_isolation_supported(void) { return false; }
static inline int hv_set_mem_host_visibility(unsigned long addr, int numpages, bool visible) { return 0; }

#endif
