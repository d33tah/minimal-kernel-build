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
	die("relocs [--abs-relocs|--realmode] vmlinux\n");
}

int main(int argc, char **argv)
{
	int show_absolute_relocs;
	int use_real_mode;
	const char *fname;
	FILE *fp;
	int i;
	unsigned char e_ident[EI_NIDENT];

	show_absolute_relocs = 0;
	use_real_mode = 0;
	fname = NULL;
	for (i = 1; i < argc; i++) {
		char *arg = argv[i];
		if (*arg == '-') {
			if (strcmp(arg, "--abs-relocs") == 0) {
				show_absolute_relocs = 1;
				continue;
			}
			if (strcmp(arg, "--realmode") == 0) {
				use_real_mode = 1;
				continue;
			}
		}
		else if (!fname) {
			fname = arg;
			continue;
		}
		usage();
	}
	if (!fname) {
		usage();
	}
	fp = fopen(fname, "r");
	if (!fp) {
		die("Cannot open %s: %s\n", fname, strerror(errno));
	}
	if (fread(&e_ident, 1, EI_NIDENT, fp) != EI_NIDENT) {
		die("Cannot read %s: %s", fname, strerror(errno));
	}
	rewind(fp);
	process_32(fp, use_real_mode, show_absolute_relocs);
	fclose(fp);
	return 0;
}
