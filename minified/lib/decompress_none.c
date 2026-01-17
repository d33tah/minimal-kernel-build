// SPDX-License-Identifier: GPL-2.0
/*
 * "Decompress" uncompressed data - just copy it.
 */

#ifdef STATIC
#define PREBOOT
STATIC int INIT __decompress(unsigned char *buf, long len,
			     long (*fill)(void *, unsigned long),
			     long (*flush)(void *, unsigned long),
			     unsigned char *out_buf, long out_len, long *pos,
			     void (*error)(char *x))
{
	/* For uncompressed, just copy the data */
	if (out_buf && buf) {
		/* Remove the 4-byte size suffix that was appended */
		long actual_len = len - 4;
		if (actual_len > 0 && actual_len <= out_len) {
			memcpy(out_buf, buf, actual_len);
		}
	}
	return 0;
}
#endif
