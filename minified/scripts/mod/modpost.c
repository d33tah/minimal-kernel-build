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

int main(int argc, char **argv)
{
	int opt;

	/* Parse options to prevent getopt errors */
	while ((opt = getopt(argc, argv, "ei:mnT:o:awENd:")) != -1) {
		/* Ignore all options */
	}

	return 0;
}
