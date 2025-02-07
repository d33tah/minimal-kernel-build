// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/mm.h>

#include "asm/pgtable_types.h"
#include "linux/mem_encrypt.h"

pgprot_t vm_get_page_prot(unsigned long vm_flags)
{
	unsigned long val = pgprot_val(protection_map[vm_flags &
				      (VM_READ|VM_WRITE|VM_EXEC|VM_SHARED)]);


	val = __sme_set(val);
	if (val & _PAGE_PRESENT)
		val &= __supported_pte_mask;
	return __pgprot(val);
}
EXPORT_SYMBOL(vm_get_page_prot);
