/* Minimal sev.h - SEV/SNP support disabled, only stubs */
#ifndef __ASM_ENCRYPTED_STATE_H
#define __ASM_ENCRYPTED_STATE_H

#include <linux/types.h>

struct pt_regs;
struct real_mode_header;
struct boot_params;

/* Stub functions - SEV not enabled */
/* sev_es_ist_enter, sev_es_ist_exit, sev_es_nmi_complete call sites removed */
static inline void setup_ghcb(void) { }

#endif
