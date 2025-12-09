
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/export.h>
#include <asm/unaligned.h>

const char hex_asc[] = "0123456789abcdef";
const char hex_asc_upper[] = "0123456789ABCDEF";

/* Stub: hex_to_bin not called in minimal kernel */
int hex_to_bin(unsigned char ch) { return -1; }

/* Stub: hex2bin not called in minimal kernel */
int hex2bin(u8 *dst, const char *src, size_t count) { return -EINVAL; }

/* Stub: bin2hex not called in minimal kernel */
char *bin2hex(char *dst, const void *src, size_t count) { return dst; }

int hex_dump_to_buffer(const void *buf, size_t len, int rowsize, int groupsize,
		       char *linebuf, size_t linebuflen, bool ascii)
{
	 
	if (linebuflen > 0)
		linebuf[0] = '\0';
	return 0;
}

