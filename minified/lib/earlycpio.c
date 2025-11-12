// SPDX-License-Identifier: GPL-2.0-only
/* Stubbed - earlycpio unused in minimal kernel */
#include <linux/earlycpio.h>

struct cpio_data find_cpio_data(const char *path, void *data, size_t len, long *offset)
{
	struct cpio_data cd = { NULL, 0, "" };
	return cd;
}
