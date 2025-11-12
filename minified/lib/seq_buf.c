// SPDX-License-Identifier: GPL-2.0
/*
 * seq_buf.c - Stubbed minimal implementation
 */
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/seq_buf.h>

int seq_buf_print_seq(struct seq_file *m, struct seq_buf *s) { return 0; }
int seq_buf_vprintf(struct seq_buf *s, const char *fmt, va_list args) { return -1; }
int seq_buf_printf(struct seq_buf *s, const char *fmt, ...) { return -1; }
EXPORT_SYMBOL_GPL(seq_buf_printf);
int seq_buf_puts(struct seq_buf *s, const char *str) { return -1; }
int seq_buf_putc(struct seq_buf *s, unsigned char c) { return -1; }
int seq_buf_putmem(struct seq_buf *s, const void *mem, unsigned int len) { return -1; }
int seq_buf_putmem_hex(struct seq_buf *s, const void *mem, unsigned int len) { return -1; }
int seq_buf_path(struct seq_buf *s, const struct path *path, const char *esc) { return -1; }
int seq_buf_to_user(struct seq_buf *s, char __user *ubuf, int cnt) { return -EBUSY; }
int seq_buf_hex_dump(struct seq_buf *s, const char *prefix_str, int prefix_type,
		     int rowsize, int groupsize, const void *buf, size_t len, bool ascii) { return -1; }
