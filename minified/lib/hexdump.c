
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/export.h>
#include <asm/unaligned.h>

/* hex_asc removed - never used, only hex_asc_upper is used by vsprintf */
const char hex_asc_upper[] = "0123456789ABCDEF";
