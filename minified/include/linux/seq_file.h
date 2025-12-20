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

/* seq_has_overflowed removed - never called */
/* seq_get_buf removed - never called */
/* seq_commit removed - never called */
/* seq_open, seq_read, seq_read_iter, seq_lseek, seq_release removed - not called */

__printf(2, 3)
void seq_printf(struct seq_file *m, const char *fmt, ...);

/* seq_user_ns removed - never called */


#define SEQ_START_TOKEN ((void *)1)

void seq_file_init(void);
#endif
