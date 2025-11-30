 
#ifndef _ASM_X86_OLPC_OFW_H
#define _ASM_X86_OLPC_OFW_H

 
#define OLPC_OFW_PDE_NR 1022

#define OLPC_OFW_SIG 0x2057464F	 

static inline void olpc_ofw_detect(void) { }
static inline void setup_olpc_ofw_pgd(void) { }
static inline void olpc_dt_build_devicetree(void) { }

#endif  
