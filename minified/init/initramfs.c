// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/init_syscalls.h>
#include <linux/initrd.h>

/*
 * Unpacks the built-in uncompressed newc ("070701") cpio archive that
 * usr/gen_init_cpio writes: regular files, directories and character device
 * nodes, each created once (no hard links, no chown, no mtime). Any other
 * entry type or a malformed archive panics instead of being skipped.
 */

#define CPIO_HDR_LEN 110

/* Header field @i (0 = c_ino ... 12 = c_check): exactly 8 hex digits. */
static unsigned long __init cpio_field(const char *hdr, int i)
{
	const char *s = hdr + 6 + 8 * i;
	unsigned long v = 0;
	int n;

	for (n = 0; n < 8; n++) {
		unsigned int c = s[n], lc = c | 0x20;

		if (c >= '0' && c <= '9')
			v = v * 16 + c - '0';
		else if (lc >= 'a' && lc <= 'f')
			v = v * 16 + lc - 'a' + 10;
		else
			panic("initramfs: bad hex digit in cpio header");
	}
	return v;
}

static void __init create_entry(const char *name, umode_t mode,
				const char *data, unsigned long size,
				unsigned int rdev)
{
	struct file *file;
	loff_t pos = 0;
	int err = 0;

	if (S_ISREG(mode)) {
		file = filp_open(name, O_WRONLY | O_CREAT, mode);
		if (IS_ERR(file))
			panic("initramfs: cannot create %s", name);
		if (kernel_write(file, data, size, &pos) != size)
			panic("initramfs: cannot write %s", name);
		fput(file);
	} else if (S_ISDIR(mode)) {
		err = init_mkdir(name, mode);
	} else if (S_ISCHR(mode)) {
		err = init_mknod(name, mode, rdev);
	} else {
		panic("initramfs: unsupported cpio entry type");
	}
	if (err)
		panic("initramfs: cannot create %s", name);
}

static void __init unpack_to_rootfs(const char *buf, unsigned long len)
{
	unsigned long off = 0, mode, size, namesize;
	const char *hdr, *name;

	for (;;) {
		hdr = buf + off;
		if (len - off < CPIO_HDR_LEN || memcmp(hdr, "070701", 6))
			panic("initramfs: no newc cpio header");
		mode = cpio_field(hdr, 1);
		size = cpio_field(hdr, 6);
		namesize = cpio_field(hdr, 11);
		name = hdr + CPIO_HDR_LEN;
		off += CPIO_HDR_LEN;
		if (namesize < 2 || namesize > len - off || name[namesize - 1])
			panic("initramfs: bad cpio file name");
		off = ALIGN(off + namesize, 4);
		if (off > len || size > len - off)
			panic("initramfs: truncated cpio archive");
		if (!strcmp(name, "TRAILER!!!"))
			break;
		create_entry(name, mode, buf + off, size,
			     new_encode_dev(MKDEV(cpio_field(hdr, 9),
						  cpio_field(hdr, 10))));
		off = ALIGN(off + size, 4);
	}
	/* gen_init_cpio pads the archive with zeros to a 512-byte multiple. */
	for (; off < len; off++)
		if (buf[off])
			panic("initramfs: data after the cpio trailer");
}

static int __init populate_rootfs(void)
{
	unpack_to_rootfs(__initramfs_start, __initramfs_size);
	return 0;
}
rootfs_initcall(populate_rootfs);
