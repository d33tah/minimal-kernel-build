 
 
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/export.h>
#include <linux/list.h>
#include <linux/uaccess.h>

int seq_open(struct file *file, const struct seq_operations *op) { return -ENOMEM; }

ssize_t seq_read(struct file *file, char __user *buf, size_t size, loff_t *ppos) { return 0; }

ssize_t seq_read_iter(struct kiocb *iocb, struct iov_iter *iter) { return 0; }

loff_t seq_lseek(struct file *file, loff_t offset, int whence) { return 0; }

int seq_release(struct inode *inode, struct file *file) { return 0; }

void seq_escape_mem(struct seq_file *m, const char *src, size_t len, unsigned int flags, const char *esc) { }

void seq_vprintf(struct seq_file *m, const char *fmt, va_list args) { }

__printf(2, 3)
void seq_printf(struct seq_file *m, const char *fmt, ...) { }

char *mangle_path(char *s, const char *p, const char *esc) { return s; }

int seq_path(struct seq_file *m, const struct path *path, const char *esc) { return 0; }

int seq_file_path(struct seq_file *m, struct file *file, const char *esc) { return 0; }

int seq_dentry(struct seq_file *m, struct dentry *dentry, const char *esc) { return 0; }

int single_open(struct file *file, int (*show)(struct seq_file *, void *), void *data) { return -ENOMEM; }

int single_open_size(struct file *file, int (*show)(struct seq_file *, void *), void *data, size_t size) { return -ENOMEM; }

int single_release(struct inode *inode, struct file *file) { return 0; }

int seq_release_private(struct inode *inode, struct file *file) { return 0; }

void *__seq_open_private(struct file *file, const struct seq_operations *ops, int psize) { return NULL; }

int seq_open_private(struct file *file, const struct seq_operations *ops, int psize) { return -ENOMEM; }

void seq_putc(struct seq_file *m, char c) { }

void seq_puts(struct seq_file *m, const char *s) { }

void seq_put_decimal_ull(struct seq_file *m, const char *delimiter, unsigned long long num) { }

void seq_put_decimal_ll(struct seq_file *m, const char *delimiter, long long num) { }

int seq_write(struct seq_file *seq, const void *data, size_t len) { return 0; }

void seq_pad(struct seq_file *m, char c) { }

void seq_hex_dump(struct seq_file *m, const char *prefix_str, int prefix_type,
		  int rowsize, int groupsize, const void *buf, size_t len, bool ascii) { }

struct list_head *seq_list_start(struct list_head *head, loff_t pos) { return NULL; }

struct list_head *seq_list_start_head(struct list_head *head, loff_t pos) { return NULL; }

struct list_head *seq_list_next(void *v, struct list_head *head, loff_t *ppos) { return NULL; }

struct list_head *seq_list_start_rcu(struct list_head *head, loff_t pos) { return NULL; }

struct list_head *seq_list_start_head_rcu(struct list_head *head, loff_t pos) { return NULL; }

struct list_head *seq_list_next_rcu(void *v, struct list_head *head, loff_t *ppos) { return NULL; }

struct hlist_node *seq_hlist_start(struct hlist_head *head, loff_t pos) { return NULL; }

struct hlist_node *seq_hlist_start_head(struct hlist_head *head, loff_t pos) { return NULL; }

struct hlist_node *seq_hlist_next(void *v, struct hlist_head *head, loff_t *ppos) { return NULL; }

struct hlist_node *seq_hlist_start_rcu(struct hlist_head *head, loff_t pos) { return NULL; }

struct hlist_node *seq_hlist_start_head_rcu(struct hlist_head *head, loff_t pos) { return NULL; }

struct hlist_node *seq_hlist_next_rcu(void *v, struct hlist_head *head, loff_t *ppos) { return NULL; }

struct hlist_node *seq_hlist_start_percpu(struct hlist_head __percpu *head, int *cpu, loff_t pos) { return NULL; }

struct hlist_node *seq_hlist_next_percpu(void *v, struct hlist_head __percpu *head, int *cpu, loff_t *pos) { return NULL; }

void seq_file_init(void) { }
