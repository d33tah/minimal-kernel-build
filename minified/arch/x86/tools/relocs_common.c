/* relocs.h inlined */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <elf.h>
#include <byteswap.h>
#define USE_BSD
#include <endian.h>
#include <regex.h>
#include <tools/le_byteshift.h>
__attribute__((__format__(printf, 1, 2))) void die(char *fmt, ...)
	__attribute__((noreturn));
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
enum symtype { S_ABS, S_REL, S_SEG, S_LIN, S_NSYMTYPES };
void process_32(FILE *fp, int use_real_mode);

void die(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(1);
}

static void usage(void)
{
	die("relocs [--realmode] vmlinux\n");
}

int main(int argc, char **argv)
{
	int use_real_mode = 0;
	const char *fname = NULL;
	FILE *fp;
	int i;

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];
		if (*arg == '-') {
			if (strcmp(arg, "--realmode") == 0) {
				use_real_mode = 1;
				continue;
			}
		} else if (!fname) {
			fname = arg;
			continue;
		}
		usage();
	}
	if (!fname)
		usage();

	fp = fopen(fname, "r");
	if (!fp)
		die("Cannot open %s: %s\n", fname, strerror(errno));

	process_32(fp, use_real_mode);
	fclose(fp);
	return 0;
}
