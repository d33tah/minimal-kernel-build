// SPDX-License-Identifier: GPL-2.0
/*
 * Writes a newc ("070701") cpio archive to stdout from a description on stdin,
 * one entry per line (names are absolute, the leading '/' is dropped):
 *
 *   file <name> <location> <octal mode> <uid> <gid>
 *   dir <name> <octal mode> <uid> <gid>
 *   nod <name> <octal mode> <uid> <gid> <c|b> <major> <minor>
 *
 * Every mtime is 0, so the archive depends only on the description and the
 * files' contents. init/initramfs.c accepts exactly this format.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static unsigned int offset, ino = 721;

static void __attribute__((noreturn)) fail(const char *what, const char *arg)
{
	fprintf(stderr, "gen_init_cpio: %s: %s\n", what, arg);
	exit(1);
}

static void pad(unsigned int align)
{
	for (; offset % align; offset++)
		putchar(0);
}

/* The trailer passes ino 0 and gets zero c_devmajor/c_devminor; entries 3:1. */
static void header(unsigned int ino, unsigned int mode, unsigned int uid,
		   unsigned int gid, unsigned int nlink, unsigned int size,
		   unsigned int maj, unsigned int min, const char *name)
{
	if (name[0] == '/')
		name++;
	offset += printf("070701%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X"
			 "%08X%08X",
			 ino, mode, uid, gid, nlink, 0, size, ino ? 3 : 0,
			 ino ? 1 : 0, maj, min, (unsigned int)strlen(name) + 1,
			 0);
	offset += printf("%s", name) + 1;
	putchar(0);
	pad(4);
}

static void file(const char *name, const char *location, unsigned int mode,
		 unsigned int uid, unsigned int gid)
{
	FILE *f = fopen(location, "rb");
	char buf[4096];
	size_t n;
	long size;

	if (!f || fseek(f, 0, SEEK_END) || (size = ftell(f)) < 0 ||
	    fseek(f, 0, SEEK_SET))
		fail("cannot read", location);
	header(ino++, S_IFREG | mode, uid, gid, 1, size, 0, 0, name);
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
		fwrite(buf, 1, n, stdout);
		offset += n;
		size -= n;
	}
	if (ferror(f) || size)
		fail("short read", location);
	fclose(f);
	pad(4);
}

int main(void)
{
	char line[8192], name[4096], location[4096], dev;
	unsigned int mode, uid, gid, maj, min;

	while (fgets(line, sizeof(line), stdin)) {
		if (line[0] == '#' || line[0] == '\n')
			continue;
		if (sscanf(line, "file %4095s %4095s %o %u %u", name, location,
			   &mode, &uid, &gid) == 5)
			file(name, location, mode, uid, gid);
		else if (sscanf(line, "dir %4095s %o %u %u", name, &mode, &uid,
				&gid) == 4)
			header(ino++, S_IFDIR | mode, uid, gid, 2, 0, 0, 0,
			       name);
		else if (sscanf(line, "nod %4095s %o %u %u %c %u %u", name,
				&mode, &uid, &gid, &dev, &maj, &min) == 7 &&
			 (dev == 'c' || dev == 'b'))
			header(ino++, (dev == 'b' ? S_IFBLK : S_IFCHR) | mode,
			       uid, gid, 1, 0, maj, min, name);
		else
			fail("unsupported line", line);
	}
	header(0, 0, 0, 0, 1, 0, 0, 0, "TRAILER!!!");
	pad(512);
	return ferror(stdout) || fflush(stdout);
}
