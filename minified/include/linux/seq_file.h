#ifndef _LINUX_SEQ_FILE_H
#define _LINUX_SEQ_FILE_H

#include <linux/types.h>

/*
 * struct seq_file is used only as an opaque pointer in this tree (the sole
 * consumer, ramfs_show_options, ignores it); its body was fully dead.
 * struct seq_operations and the SEQ_* macros had zero consumers and were
 * removed.
 */
struct seq_file;

#endif
