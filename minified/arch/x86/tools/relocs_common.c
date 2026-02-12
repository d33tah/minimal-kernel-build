#include "relocs.h"

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
