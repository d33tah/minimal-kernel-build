#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>
#include "modpost.h"

void *do_nofail(void *ptr, const char *expr)
{
	if (!ptr)
		exit(1);
	return ptr;
}

void buf_printf(struct buffer *buf, const char *fmt, ...)
{
}

void buf_write(struct buffer *buf, const char *s, int len)
{
}

void modpost_log(enum loglevel loglevel, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	if (loglevel == LOG_FATAL)
		exit(1);
}

void handle_moddevtable(void *mod, void *info, void *sym, const char *symname)
{
}
void add_moddevtable(struct buffer *buf, void *mod)
{
	buf_printf(buf, "\n");
}
void get_src_version(const char *modname, char sum[], unsigned sumlen)
{
}

int main(int argc, char **argv)
{
	int opt;

	while ((opt = getopt(argc, argv, "ei:mnT:o:awENd:")) != -1) {
	}

	return 0;
}
