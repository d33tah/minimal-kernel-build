

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <tools/le_byteshift.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define DEFAULT_MAJOR_ROOT 0
#define DEFAULT_MINOR_ROOT 0
#define DEFAULT_ROOT_DEV (DEFAULT_MAJOR_ROOT << 8 | DEFAULT_MINOR_ROOT)

#define SETUP_SECT_MIN 5
#define SETUP_SECT_MAX 64

u8 buf[SETUP_SECT_MAX * 512];

static unsigned long kernel_info;

static void die(const char *str, ...)
{
	va_list args;
	va_start(args, str);
	vfprintf(stderr, str, args);
	va_end(args);
	fputc('\n', stderr);
	exit(1);
}

static void usage(void)
{
	die("Usage: build setup system zoffset.h image");
}

#define PARSE_ZOFS(p, sym)                                                  \
	do {                                                                \
		if (!strncmp(p, "#define ZO_" #sym " ", 11 + sizeof(#sym))) \
			sym = strtoul(p + 11 + sizeof(#sym), NULL, 16);     \
	} while (0)

static void parse_zoffset(char *fname)
{
	FILE *file;
	char *p;
	int c;

	file = fopen(fname, "r");
	if (!file)
		die("Unable to open `%s': %m", fname);
	c = fread(buf, 1, sizeof(buf) - 1, file);
	if (ferror(file))
		die("read-error on `zoffset.h'");
	fclose(file);
	buf[c] = 0;

	p = (char *)buf;

	while (p && *p) {
		PARSE_ZOFS(p, kernel_info);

		p = strchr(p, '\n');
		while (p && (*p == '\r' || *p == '\n'))
			p++;
	}
}

int main(int argc, char **argv)
{
	unsigned int i, sz, setup_sectors;
	int c;
	u32 sys_size;
	struct stat sb;
	FILE *file, *dest;
	int fd;
	void *kernel;

	if (argc != 5)
		usage();
	parse_zoffset(argv[3]);

	dest = fopen(argv[4], "w");
	if (!dest)
		die("Unable to write `%s': %m", argv[4]);

	file = fopen(argv[1], "r");
	if (!file)
		die("Unable to open `%s': %m", argv[1]);
	c = fread(buf, 1, sizeof(buf), file);
	if (ferror(file))
		die("read-error on `setup'");
	if (c < 1024)
		die("The setup must be at least 1024 bytes");
	if (get_unaligned_le16(&buf[510]) != 0xAA55)
		die("Boot block hasn't got boot flag (0xAA55)");
	fclose(file);

	setup_sectors = (c + 511) / 512;
	if (setup_sectors < SETUP_SECT_MIN)
		setup_sectors = SETUP_SECT_MIN;
	i = setup_sectors * 512;
	memset(buf + c, 0, i - c);

	put_unaligned_le16(DEFAULT_ROOT_DEV, &buf[508]);

	fd = open(argv[2], O_RDONLY);
	if (fd < 0)
		die("Unable to open `%s': %m", argv[2]);
	if (fstat(fd, &sb))
		die("Unable to stat `%s': %m", argv[2]);
	sz = sb.st_size;
	kernel = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);
	if (kernel == MAP_FAILED)
		die("Unable to mmap '%s': %m", argv[2]);

	sys_size = (sz + 15) / 16;

	buf[0x1f1] = setup_sectors - 1;
	put_unaligned_le32(sys_size, &buf[0x1f4]);

	put_unaligned_le32(kernel_info, &buf[0x268]);

	if (fwrite(buf, 1, i, dest) != i)
		die("Writing setup failed");

	if (fwrite(kernel, 1, sz, dest) != sz)
		die("Writing kernel failed");

	while (sz++ < sys_size * 16) {
		if (fwrite("\0", 1, 1, dest) != 1)
			die("Writing padding failed");
	}

	if (fclose(dest))
		die("Writing image failed");

	close(fd);

	return 0;
}
