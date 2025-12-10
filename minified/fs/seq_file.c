#include <linux/seq_file.h>
#include <linux/fs.h>

/* seq_open, seq_read, seq_read_iter, seq_lseek, seq_release removed - not called */
__printf(2, 3) void seq_printf(struct seq_file *m, const char *fmt, ...) { }
void seq_file_init(void) { }
