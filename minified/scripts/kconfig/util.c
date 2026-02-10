
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "lkc.h"

struct file *file_lookup(const char *name)
{
	struct file *file;

	for (file = file_list; file; file = file->next) {
		if (!strcmp(name, file->name)) {
			return file;
		}
	}

	file = xmalloc(sizeof(*file));
	memset(file, 0, sizeof(*file));
	file->name = xstrdup(name);
	file->next = file_list;
	file_list = file;
	return file;
}

/* gstr functions (str_new, str_append, str_printf, str_get) removed - never called */

void *xmalloc(size_t size)
{
	void *p = malloc(size);
	if (p)
		return p;
	fprintf(stderr, "Out of memory.\n");
	exit(1);
}

void *xcalloc(size_t nmemb, size_t size)
{
	void *p = calloc(nmemb, size);
	if (p)
		return p;
	fprintf(stderr, "Out of memory.\n");
	exit(1);
}

void *xrealloc(void *p, size_t size)
{
	p = realloc(p, size);
	if (p)
		return p;
	fprintf(stderr, "Out of memory.\n");
	exit(1);
}

char *xstrdup(const char *s)
{
	char *p;

	p = strdup(s);
	if (p)
		return p;
	fprintf(stderr, "Out of memory.\n");
	exit(1);
}

char *xstrndup(const char *s, size_t n)
{
	char *p;

	p = strndup(s, n);
	if (p)
		return p;
	fprintf(stderr, "Out of memory.\n");
	exit(1);
}
