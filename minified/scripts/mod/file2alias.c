/* Stubbed - modules disabled */
#include "modpost.h"

/* Empty function - no module aliases needed without CONFIG_MODULES */
void handle_moddevtable(struct module *mod, struct elf_info *info,
			Elf_Sym *sym, const char *symname)
{
	/* No-op for modular device tables */
}
