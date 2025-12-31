 
 

#ifndef __X86_MEM_ENCRYPT_H__
#define __X86_MEM_ENCRYPT_H__

#ifndef __ASSEMBLY__

#include <linux/init.h>

#include <asm/bootparam.h>


#define sme_me_mask	0ULL

/* sev_setup_arch, sev_es_init_vc_handling, mem_encrypt_free_decrypted_mem removed */

#define __bss_decrypted


 
void __init mem_encrypt_init(void);

 
#define __sme_pa(x)		(__pa(x) | sme_me_mask)
#define __sme_pa_nodebug(x)	(__pa_nodebug(x) | sme_me_mask)

extern char __start_bss_decrypted[], __end_bss_decrypted[], __start_bss_decrypted_unused[];


#endif	 

#endif	 
