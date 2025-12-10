#include <linux/seq_file.h>
#include <linux/fs.h>

int seq_open(struct file *file, const struct seq_operations *op) { return -ENOMEM; }
ssize_t seq_read(struct file *file, char __user *buf, size_t size, loff_t *ppos) { return 0; }
ssize_t seq_read_iter(struct kiocb *iocb, struct iov_iter *iter) { return 0; }
loff_t seq_lseek(struct file *file, loff_t offset, int whence) { return 0; }
int seq_release(struct inode *inode, struct file *file) { return 0; }
__printf(2, 3) void seq_printf(struct seq_file *m, const char *fmt, ...) { }
void seq_file_init(void) { }
