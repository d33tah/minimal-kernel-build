
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/export.h>
#include <asm/unaligned.h>

const char hex_asc[] = "0123456789abcdef";
const char hex_asc_upper[] = "0123456789ABCDEF";

/* Removed: hex_to_bin, hex2bin, bin2hex - never called (~6 LOC) */

int hex_dump_to_buffer(const void *buf, size_t len, int rowsize, int groupsize,
		       char *linebuf, size_t linebuflen, bool ascii)
{
	 
	if (linebuflen > 0)
		linebuf[0] = '\0';
	return 0;
}

