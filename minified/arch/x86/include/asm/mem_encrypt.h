 
 

#ifndef __X86_MEM_ENCRYPT_H__
#define __X86_MEM_ENCRYPT_H__

#ifndef __ASSEMBLY__

#include <linux/init.h>

#include <asm/bootparam.h>


#define sme_me_mask	0ULL

/* sev_setup_arch, sev_es_init_vc_handling, mem_encrypt_free_decrypted_mem,
   __bss_decrypted, __sme_pa_nodebug removed - unused */
/* mem_encrypt_init removed - never called */

#define __sme_pa(x)		(__pa(x) | sme_me_mask)

/* __start_bss_decrypted, __end_bss_decrypted, __start_bss_decrypted_unused removed - unused */


#endif	 

#endif	 
