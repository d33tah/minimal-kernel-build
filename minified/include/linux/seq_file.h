#ifndef _LINUX_SEQ_FILE_H
#define _LINUX_SEQ_FILE_H

#include <linux/types.h>
#include <linux/string.h>
#include <linux/string_helpers.h>
#include <linux/bug.h>
#include <linux/mutex.h>
#include <linux/cpumask.h>
#include <linux/nodemask.h>
#include <linux/fs.h>
#include <linux/cred.h>

struct seq_operations;

struct seq_file {
	char *buf;
	size_t size;
	size_t from;
	size_t count;
	size_t pad_until;
	loff_t index;
	loff_t read_pos;
	struct mutex lock;
	const struct seq_operations *op;
	int poll_event;
	const struct file *file;
	void *private;
};

struct seq_operations {
	void * (*start) (struct seq_file *m, loff_t *pos);
	void (*stop) (struct seq_file *m, void *v);
	void * (*next) (struct seq_file *m, void *v, loff_t *pos);
	int (*show) (struct seq_file *m, void *v);
};

#define SEQ_SKIP 1

static inline bool seq_has_overflowed(struct seq_file *m)
{
	return m->count == m->size;
}

static inline size_t seq_get_buf(struct seq_file *m, char **bufp)
{
	BUG_ON(m->count > m->size);
	if (m->count < m->size)
		*bufp = m->buf + m->count;
	else
		*bufp = NULL;

	return m->size - m->count;
}

static inline void seq_commit(struct seq_file *m, int num)
{
	if (num < 0) {
		m->count = m->size;
	} else {
		BUG_ON(m->count + num > m->size);
		m->count += num;
	}
}

/* seq_setwidth, seq_pad, mangle_path, seq_vprintf removed - unused */
/* seq_open, seq_read, seq_read_iter, seq_lseek, seq_release removed - not called */

__printf(2, 3)
void seq_printf(struct seq_file *m, const char *fmt, ...);
/* seq_putc, seq_puts, seq_put_decimal_ull, seq_put_decimal_ull_width, seq_put_decimal_ll, seq_put_hex_ll removed - unused */
/* seq_escape_mem, seq_escape_str, seq_escape, seq_hex_dump removed - unused */
/* single_start, single_open, single_open_size, single_release removed - unused */
/* DEFINE_SEQ_ATTRIBUTE, DEFINE_SHOW_ATTRIBUTE, DEFINE_PROC_SHOW_ATTRIBUTE removed - unused */

static inline struct user_namespace *seq_user_ns(struct seq_file *seq)
{
	extern struct user_namespace init_user_ns;
	return &init_user_ns;
}

/* seq_show_option and seq_show_option_n removed - unused */

#define SEQ_START_TOKEN ((void *)1)

void seq_file_init(void);
#endif
