#ifndef _LINUX_SEQ_FILE_H
#define _LINUX_SEQ_FILE_H

#include <linux/types.h>

struct seq_operations;

/*
 * struct seq_file is used only as an opaque pointer in this tree (the sole
 * consumer, ramfs_show_options, ignores it); its body was fully dead.
 */
struct seq_file;

struct seq_operations {
	void * (*start) (struct seq_file *m, loff_t *pos);
	void (*stop) (struct seq_file *m, void *v);
	void * (*next) (struct seq_file *m, void *v, loff_t *pos);
	int (*show) (struct seq_file *m, void *v);
};

#define SEQ_SKIP 1




#define SEQ_START_TOKEN ((void *)1)

#endif
