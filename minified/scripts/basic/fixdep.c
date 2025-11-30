 

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

static void usage(void)
{
	fprintf(stderr, "Usage: fixdep <depfile> <target> <cmdline>\n");
	exit(1);
}

struct item {
	struct item	*next;
	unsigned int	len;
	unsigned int	hash;
	char		name[];
};

#define HASHSZ 256
static struct item *hashtab[HASHSZ];

static unsigned int strhash(const char *str, unsigned int sz)
{
	 
	unsigned int i, hash = 2166136261U;

	for (i = 0; i < sz; i++)
		hash = (hash ^ str[i]) * 0x01000193;
	return hash;
}

 
static int is_defined_config(const char *name, int len, unsigned int hash)
{
	struct item *aux;

	for (aux = hashtab[hash % HASHSZ]; aux; aux = aux->next) {
		if (aux->hash == hash && aux->len == len &&
		    memcmp(aux->name, name, len) == 0)
			return 1;
	}
	return 0;
}

 
static void define_config(const char *name, int len, unsigned int hash)
{
	struct item *aux = malloc(sizeof(*aux) + len);

	if (!aux) {
		perror("fixdep:malloc");
		exit(1);
	}
	memcpy(aux->name, name, len);
	aux->len = len;
	aux->hash = hash;
	aux->next = hashtab[hash % HASHSZ];
	hashtab[hash % HASHSZ] = aux;
}

 
static void use_config(const char *m, int slen)
{
	unsigned int hash = strhash(m, slen);

	if (is_defined_config(m, slen, hash))
	    return;

	define_config(m, slen, hash);
	 
	printf("    $(wildcard include/config/%.*s) \\\n", slen, m);
}

 
static int str_ends_with(const char *s, int slen, const char *sub)
{
	int sublen = strlen(sub);

	if (sublen > slen)
		return 0;

	return !memcmp(s + slen - sublen, sub, sublen);
}

static void parse_config_file(const char *p)
{
	const char *q, *r;
	const char *start = p;

	while ((p = strstr(p, "CONFIG_"))) {
		if (p > start && (isalnum(p[-1]) || p[-1] == '_')) {
			p += 7;
			continue;
		}
		p += 7;
		q = p;
		while (isalnum(*q) || *q == '_')
			q++;
		if (str_ends_with(p, q - p, "_MODULE"))
			r = q - 7;
		else
			r = q;
		if (r > p)
			use_config(p, r - p);
		p = q;
	}
}

static void *read_file(const char *filename)
{
	struct stat st;
	int fd;
	char *buf;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "fixdep: error opening file: ");
		perror(filename);
		exit(2);
	}
	if (fstat(fd, &st) < 0) {
		fprintf(stderr, "fixdep: error fstat'ing file: ");
		perror(filename);
		exit(2);
	}
	buf = malloc(st.st_size + 1);
	if (!buf) {
		perror("fixdep: malloc");
		exit(2);
	}
	if (read(fd, buf, st.st_size) != st.st_size) {
		perror("fixdep: read");
		exit(2);
	}
	buf[st.st_size] = '\0';
	close(fd);

	return buf;
}

 
static int is_ignored_file(const char *s, int len)
{
	return str_ends_with(s, len, "include/generated/autoconf.h") ||
	       str_ends_with(s, len, "include/generated/autoksyms.h");
}

 
static void parse_dep_file(char *m, const char *target)
{
	char *p;
	int is_last, is_target;
	int saw_any_target = 0;
	int is_first_dep = 0;
	void *buf;

	while (1) {
		 
		while (*m == ' ' || *m == '\\' || *m == '\n')
			m++;

		if (!*m)
			break;

		 
		p = m;
		while (*p && *p != ' ' && *p != '\\' && *p != '\n')
			p++;
		is_last = (*p == '\0');
		 
		is_target = (*(p-1) == ':');
		 
		if (is_target) {
			 
			is_first_dep = 1;
		} else if (!is_ignored_file(m, p - m)) {
			*p = '\0';

			 
			if (is_first_dep) {
				 
				if (!saw_any_target) {
					saw_any_target = 1;
					printf("source_%s := %s\n\n",
					       target, m);
					printf("deps_%s := \\\n", target);
				}
				is_first_dep = 0;
			} else {
				printf("  %s \\\n", m);
			}

			buf = read_file(m);
			parse_config_file(buf);
			free(buf);
		}

		if (is_last)
			break;

		 
		m = p + 1;
	}

	if (!saw_any_target) {
		fprintf(stderr, "fixdep: parse error; no targets found\n");
		exit(1);
	}

	printf("\n%s: $(deps_%s)\n\n", target, target);
	printf("$(deps_%s):\n", target);
}

int main(int argc, char *argv[])
{
	const char *depfile, *target, *cmdline;
	void *buf;

	if (argc != 4)
		usage();

	depfile = argv[1];
	target = argv[2];
	cmdline = argv[3];

	printf("cmd_%s := %s\n\n", target, cmdline);

	buf = read_file(depfile);
	parse_dep_file(buf, target);
	free(buf);

	fflush(stdout);

	 
	if (ferror(stdout)) {
		fprintf(stderr, "fixdep: not all data was written to the output\n");
		exit(1);
	}

	return 0;
}
