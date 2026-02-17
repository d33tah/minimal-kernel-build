#include "modpost.h"
void handle_moddevtable(void *mod, void *info, void *sym, const char *symname)
{
}
void add_moddevtable(struct buffer *buf, void *mod)
{
	buf_printf(buf, "\n");
}
