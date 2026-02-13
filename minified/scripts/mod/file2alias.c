#include "modpost.h"
void handle_moddevtable(struct module *mod, struct elf_info *info, Elf_Sym *sym,
			const char *symname)
{
}
void add_moddevtable(struct buffer *buf, struct module *mod)
{
	buf_printf(buf, "\n");
}
