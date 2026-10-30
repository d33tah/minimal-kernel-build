#include <linux/init.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/init_syscalls.h>

static __initdata bool csum_present;
static __initdata u32 io_csum;

static ssize_t __init xwrite(struct file *file, const unsigned char *p,
			     size_t count, loff_t *pos)
{
	ssize_t out = 0;

	while (count) {
		ssize_t rv = kernel_write(file, p, count, pos);

		if (rv < 0) {
			if (rv == -EINTR || rv == -EAGAIN)
				continue;
			return out ? out : rv;
		} else if (rv == 0)
			break;

		if (csum_present) {
			ssize_t i;

			for (i = 0; i < rv; i++)
				io_csum += p[i];
		}

		p += rv;
		out += rv;
		count -= rv;
	}

	return out;
}

static __initdata char *message;
static void __init error(char *x)
{
	if (!message)
		message = x;
}

static void panic_show_mem(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	panic(fmt, args);
}

#define N_ALIGN(len) ((((len) + 1) & ~3) + 2)

static __initdata umode_t mode;
static __initdata unsigned long body_len, name_len;
static __initdata unsigned rdev;
static __initdata u32 hdr_csum;

/* One newc header field: up to 8 hex digits (gen_init_cpio writes %08X). */
static unsigned long __init parse_hex8(const char *s)
{
	unsigned long v = 0;
	int i;

	for (i = 0; i < 8; i++) {
		unsigned int c = s[i], lc = c | 0x20;

		if (c >= '0' && c <= '9')
			v = v * 16 + c - '0';
		else if (lc >= 'a' && lc <= 'f')
			v = v * 16 + lc - 'a' + 10;
		else
			break;
	}
	return v;
}

static void __init parse_header(char *s)
{
	unsigned long parsed[13];
	int i;

	for (i = 0, s += 6; i < 13; i++, s += 8)
		parsed[i] = parse_hex8(s);
	mode = parsed[1];
	body_len = parsed[6];
	rdev = new_encode_dev(MKDEV(parsed[9], parsed[10]));
	name_len = parsed[11];
	hdr_csum = parsed[12];
}

static __initdata enum state {
	Start,
	Collect,
	GotHeader,
	SkipIt,
	GotName,
	CopyFile,
	Reset
} state,
	next_state;

static __initdata char *victim;
static unsigned long byte_count __initdata;
static __initdata loff_t this_header, next_header;

static inline void __init eat(unsigned n)
{
	victim += n;
	this_header += n;
	byte_count -= n;
}

static __initdata char *collected;
static long remains __initdata;
static __initdata char *collect;

static void __init read_into(char *buf, unsigned size, enum state next)
{
	if (byte_count >= size) {
		collected = victim;
		eat(size);
		state = next;
	} else {
		collect = collected = buf;
		remains = size;
		next_state = next;
		state = Collect;
	}
}

static __initdata char *header_buf, *name_buf;

static int __init do_start(void)
{
	read_into(header_buf, 110, GotHeader);
	return 0;
}

static int __init do_collect(void)
{
	unsigned long n = remains;
	if (byte_count < n)
		n = byte_count;
	memcpy(collect, victim, n);
	eat(n);
	collect += n;
	if ((remains -= n) != 0)
		return 1;
	state = next_state;
	return 0;
}

static int __init do_header(void)
{
	if (!memcmp(collected, "070701", 6)) {
		csum_present = false;
	} else if (!memcmp(collected, "070702", 6)) {
		csum_present = true;
	} else {
		if (memcmp(collected, "070707", 6) == 0)
			error("incorrect cpio method used: use -H newc option");
		else
			error("no cpio magic");
		return 1;
	}
	parse_header(collected);
	next_header = this_header + N_ALIGN(name_len) + body_len;
	next_header = (next_header + 3) & ~3;
	state = SkipIt;
	if (name_len <= 0 || name_len > PATH_MAX)
		return 0;
	if (S_ISREG(mode) || !body_len)
		read_into(name_buf, N_ALIGN(name_len), GotName);
	return 0;
}

static int __init do_skip(void)
{
	if (this_header + byte_count < next_header) {
		eat(byte_count);
		return 1;
	} else {
		eat(next_header - this_header);
		state = next_state;
		return 0;
	}
}

static int __init do_reset(void)
{
	while (byte_count && *victim == '\0')
		eat(1);
	if (byte_count && (this_header & 3))
		error("broken padding");
	return 1;
}

static __initdata struct file *wfile;
static __initdata loff_t wfile_pos;

static int __init do_name(void)
{
	state = SkipIt;
	next_state = Reset;
	if (strcmp(collected, "TRAILER!!!") == 0)
		return 0;
	/*
	 * Only the entry types gen_init_cpio emits for this initramfs are
	 * created: regular files, directories and device nodes. Ownership is
	 * always root (no chown), there are no hard links or symlinks, and
	 * every name is new (no clean_path/unlink).
	 */
	if (S_ISREG(mode)) {
		wfile = filp_open(collected, O_WRONLY | O_CREAT, mode);
		if (IS_ERR(wfile))
			return 0;
		wfile_pos = 0;
		io_csum = 0;
		state = CopyFile;
	} else if (S_ISDIR(mode)) {
		init_mkdir(collected, mode);
	} else if (S_ISBLK(mode) || S_ISCHR(mode) || S_ISFIFO(mode) ||
		   S_ISSOCK(mode)) {
		init_mknod(collected, mode, rdev);
	}
	return 0;
}

static int __init do_copy(void)
{
	if (byte_count >= body_len) {
		if (xwrite(wfile, victim, body_len, &wfile_pos) != body_len)
			error("write error");

		fput(wfile);
		if (csum_present && io_csum != hdr_csum)
			error("bad data checksum");
		eat(body_len);
		state = SkipIt;
		return 0;
	} else {
		if (xwrite(wfile, victim, byte_count, &wfile_pos) != byte_count)
			error("write error");
		body_len -= byte_count;
		eat(byte_count);
		return 1;
	}
}

static __initdata int (*actions[])(void) = {
	[Start] = do_start, [Collect] = do_collect, [GotHeader] = do_header,
	[SkipIt] = do_skip, [GotName] = do_name,    [CopyFile] = do_copy,
	[Reset] = do_reset,
};

static long __init write_buffer(char *buf, unsigned long len)
{
	byte_count = len;
	victim = buf;

	while (!actions[state]())
		;
	return len - byte_count;
}

static char *__init unpack_to_rootfs(char *buf, unsigned long len)
{
	long written;

	header_buf = kmalloc(110, GFP_KERNEL);
	name_buf = kmalloc(N_ALIGN(PATH_MAX), GFP_KERNEL);

	if (!header_buf || !name_buf)
		panic_show_mem("can't allocate buffers");

	state = Start;
	this_header = 0;
	message = NULL;
	while (!message && len) {
		if (*buf == '0' && !(this_header & 3)) {
			state = Start;
			written = write_buffer(buf, len);
			buf += written;
			len -= written;
			continue;
		}
		if (!*buf) {
			buf++;
			len--;
			this_header++;
			continue;
		}
		this_header = 0;
		/*
		 * Anchor-stub: the compressed-archive path is runtime-dead --
		 * the embedded initramfs is uncompressed cpio (every record
		 * begins with the '070701' magic, so *buf == '0' above), so
		 * decompress_method() always returned NULL and flush_buffer()
		 * never fired. Any byte here that is neither cpio-magic nor a
		 * padding NUL is corruption.
		 */
		error("invalid magic at start of archive");
		break;
	}
	kfree(name_buf);
	kfree(header_buf);
	return message;
}

#include <linux/initrd.h>

static void __init do_populate_rootfs(void)
{
	char *err = unpack_to_rootfs(__initramfs_start, __initramfs_size);
	if (err)
		panic_show_mem("%s", err);

	if (!initrd_start || IS_ENABLED(CONFIG_INITRAMFS_FORCE))
		goto done;

	printk(KERN_INFO "Unpacking initramfs...\n");

	err = unpack_to_rootfs((char *)initrd_start, initrd_end - initrd_start);
	if (err) {
		printk(KERN_EMERG "Initramfs unpacking failed: %s\n", err);
	}

done:

	if (initrd_start)
		free_reserved_area((void *)initrd_start, (void *)initrd_end,
				   POISON_FREE_INITMEM, "initrd");
	initrd_start = 0;
	initrd_end = 0;

	flush_delayed_fput();
}

static bool __initdata initramfs_done;

void wait_for_initramfs(void)
{
	if (!initramfs_done) {
		pr_warn_once(
			"wait_for_initramfs() called before rootfs_initcalls\n");
		return;
	}
}

static int __init populate_rootfs(void)
{
	/* The async scheduler stub ran synchronously; do so directly. */
	do_populate_rootfs();
	initramfs_done = true;
	return 0;
}
rootfs_initcall(populate_rootfs);
