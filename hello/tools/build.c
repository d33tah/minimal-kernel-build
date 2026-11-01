/*
 * Glue setup.bin and the flat 32-bit kernel into a bzImage.
 * Modelled on arch/x86/boot/tools/build.c: pad the setup to whole 512-byte
 * sectors (at least SETUP_SECT_MIN, as upstream, for old loaders), patch
 * setup_sects and syssize into the header, append the kernel and pad it to
 * a multiple of 16 bytes so that syssize covers it exactly. init_size is
 * patched too, from the size of the loaded image including .bss and stack
 * (the linker's _end minus code32_start), rounded up to 4 KiB; upstream
 * derives it from the decompressor's link map the same way.
 *
 * Usage: build setup.bin kernel.bin image_size bzImage
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SETUP_SECT_MIN 5
#define SETUP_SECT_MAX 64

static unsigned char setup[SETUP_SECT_MAX * 512];

static _Noreturn void die(const char *msg, const char *arg)
{
	fprintf(stderr, "build: %s%s\n", msg, arg ? arg : "");
	exit(1);
}

static unsigned int get_le(const unsigned char *p, int n)
{
	unsigned int v = 0;

	while (n--)
		v = v << 8 | p[n];
	return v;
}

static void put_le(unsigned char *p, unsigned int v, int n)
{
	while (n--) {
		*p++ = v & 0xff;
		v >>= 8;
	}
}

static unsigned char *slurp(const char *name, size_t *len)
{
	FILE *f = fopen(name, "rb");
	unsigned char *buf;
	long n;

	if (!f || fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 ||
	    fseek(f, 0, SEEK_SET))
		die("cannot read ", name);
	buf = malloc(n + 16);
	if (!buf || fread(buf, 1, n, f) != (size_t)n)
		die("cannot read ", name);
	fclose(f);
	*len = n;
	return buf;
}

int main(int argc, char **argv)
{
	size_t setup_len, sys_len, padded;
	unsigned int sectors, sys_size, init_size;
	unsigned char *sys, *buf;
	FILE *out;

	if (argc != 5)
		die("usage: build setup.bin kernel.bin image_size bzImage", NULL);

	buf = slurp(argv[1], &setup_len);
	if (setup_len < 0x268 || setup_len > sizeof(setup))
		die("setup must hold the header and fit in 32 KiB: ", argv[1]);
	memcpy(setup, buf, setup_len);
	free(buf);
	if (get_le(&setup[0x1fe], 2) != 0xaa55)
		die("boot block has no boot flag (0xAA55): ", argv[1]);
	if (memcmp(&setup[0x202], "HdrS", 4))
		die("no HdrS header at 0x202: ", argv[1]);

	sectors = (setup_len + 511) / 512;
	if (sectors < SETUP_SECT_MIN)
		sectors = SETUP_SECT_MIN;

	sys = slurp(argv[2], &sys_len);
	sys_size = (sys_len + 15) / 16;
	padded = (size_t)sys_size * 16;
	memset(sys + sys_len, 0, padded - sys_len);
	init_size = (strtoul(argv[3], NULL, 0) + 4095) & ~4095u;
	if (padded > init_size)
		die("image_size is smaller than the kernel file: ", argv[3]);

	setup[0x1f1] = sectors - 1;
	put_le(&setup[0x1f4], sys_size, 4);
	put_le(&setup[0x260], init_size, 4);

	out = fopen(argv[4], "wb");
	if (!out || fwrite(setup, 1, sectors * 512, out) != sectors * 512 ||
	    fwrite(sys, 1, padded, out) != padded || fclose(out))
		die("cannot write ", argv[4]);

	printf("%s: setup %u sectors, system %zu bytes (syssize 0x%x), "
	       "init_size 0x%x\n",
	       argv[4], sectors, padded, sys_size, init_size);
	return 0;
}
