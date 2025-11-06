// SPDX-License-Identifier: GPL-2.0
/* Stubbed seq_file.c */
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/export.h>
#include <linux/list.h>
#include <linux/uaccess.h>

int seq_open(struct file *file, const struct seq_operations *op) { return -ENOMEM; }
EXPORT_SYMBOL(seq_open);

ssize_t seq_read(struct file *file, char __user *buf, size_t size, loff_t *ppos) { return 0; }
EXPORT_SYMBOL(seq_read);

ssize_t seq_read_iter(struct kiocb *iocb, struct iov_iter *iter) { return 0; }
EXPORT_SYMBOL(seq_read_iter);

loff_t seq_lseek(struct file *file, loff_t offset, int whence) { return 0; }
EXPORT_SYMBOL(seq_lseek);

int seq_release(struct inode *inode, struct file *file) { return 0; }
EXPORT_SYMBOL(seq_release);

void seq_escape_mem(struct seq_file *m, const char *src, size_t len, unsigned int flags, const char *esc) { }
EXPORT_SYMBOL(seq_escape_mem);

void seq_vprintf(struct seq_file *m, const char *fmt, va_list args) { }
EXPORT_SYMBOL(seq_vprintf);

__printf(2, 3)
void seq_printf(struct seq_file *m, const char *fmt, ...) { }
EXPORT_SYMBOL(seq_printf);

char *mangle_path(char *s, const char *p, const char *esc) { return s; }
EXPORT_SYMBOL(mangle_path);

int seq_path(struct seq_file *m, const struct path *path, const char *esc) { return 0; }
EXPORT_SYMBOL(seq_path);

int seq_file_path(struct seq_file *m, struct file *file, const char *esc) { return 0; }
EXPORT_SYMBOL(seq_file_path);

int seq_dentry(struct seq_file *m, struct dentry *dentry, const char *esc) { return 0; }
EXPORT_SYMBOL(seq_dentry);

int single_open(struct file *file, int (*show)(struct seq_file *, void *), void *data) { return -ENOMEM; }
EXPORT_SYMBOL(single_open);

int single_open_size(struct file *file, int (*show)(struct seq_file *, void *), void *data, size_t size) { return -ENOMEM; }
EXPORT_SYMBOL(single_open_size);

int single_release(struct inode *inode, struct file *file) { return 0; }
EXPORT_SYMBOL(single_release);

int seq_release_private(struct inode *inode, struct file *file) { return 0; }
EXPORT_SYMBOL(seq_release_private);

void *__seq_open_private(struct file *file, const struct seq_operations *ops, int psize) { return NULL; }
EXPORT_SYMBOL(__seq_open_private);

int seq_open_private(struct file *file, const struct seq_operations *ops, int psize) { return -ENOMEM; }
EXPORT_SYMBOL(seq_open_private);

void seq_putc(struct seq_file *m, char c) { }
EXPORT_SYMBOL(seq_putc);

void seq_puts(struct seq_file *m, const char *s) { }
EXPORT_SYMBOL(seq_puts);

void seq_put_decimal_ull(struct seq_file *m, const char *delimiter, unsigned long long num) { }
EXPORT_SYMBOL(seq_put_decimal_ull);

void seq_put_decimal_ll(struct seq_file *m, const char *delimiter, long long num) { }
EXPORT_SYMBOL(seq_put_decimal_ll);

int seq_write(struct seq_file *seq, const void *data, size_t len) { return 0; }
EXPORT_SYMBOL(seq_write);

void seq_pad(struct seq_file *m, char c) { }
EXPORT_SYMBOL(seq_pad);

void seq_hex_dump(struct seq_file *m, const char *prefix_str, int prefix_type,
		  int rowsize, int groupsize, const void *buf, size_t len, bool ascii) { }
EXPORT_SYMBOL(seq_hex_dump);

struct list_head *seq_list_start(struct list_head *head, loff_t pos) { return NULL; }
EXPORT_SYMBOL(seq_list_start);

struct list_head *seq_list_start_head(struct list_head *head, loff_t pos) { return NULL; }
EXPORT_SYMBOL(seq_list_start_head);

struct list_head *seq_list_next(void *v, struct list_head *head, loff_t *ppos) { return NULL; }
EXPORT_SYMBOL(seq_list_next);

struct list_head *seq_list_start_rcu(struct list_head *head, loff_t pos) { return NULL; }
EXPORT_SYMBOL(seq_list_start_rcu);

struct list_head *seq_list_start_head_rcu(struct list_head *head, loff_t pos) { return NULL; }
EXPORT_SYMBOL(seq_list_start_head_rcu);

struct list_head *seq_list_next_rcu(void *v, struct list_head *head, loff_t *ppos) { return NULL; }
EXPORT_SYMBOL(seq_list_next_rcu);

struct hlist_node *seq_hlist_start(struct hlist_head *head, loff_t pos) { return NULL; }
EXPORT_SYMBOL(seq_hlist_start);

struct hlist_node *seq_hlist_start_head(struct hlist_head *head, loff_t pos) { return NULL; }
EXPORT_SYMBOL(seq_hlist_start_head);

struct hlist_node *seq_hlist_next(void *v, struct hlist_head *head, loff_t *ppos) { return NULL; }
EXPORT_SYMBOL(seq_hlist_next);

struct hlist_node *seq_hlist_start_rcu(struct hlist_head *head, loff_t pos) { return NULL; }
EXPORT_SYMBOL(seq_hlist_start_rcu);

struct hlist_node *seq_hlist_start_head_rcu(struct hlist_head *head, loff_t pos) { return NULL; }
EXPORT_SYMBOL(seq_hlist_start_head_rcu);

struct hlist_node *seq_hlist_next_rcu(void *v, struct hlist_head *head, loff_t *ppos) { return NULL; }
EXPORT_SYMBOL(seq_hlist_next_rcu);

struct hlist_node *seq_hlist_start_percpu(struct hlist_head __percpu *head, int *cpu, loff_t pos) { return NULL; }
EXPORT_SYMBOL(seq_hlist_start_percpu);

struct hlist_node *seq_hlist_next_percpu(void *v, struct hlist_head __percpu *head, int *cpu, loff_t *pos) { return NULL; }
EXPORT_SYMBOL(seq_hlist_next_percpu);

void seq_file_init(void) { }
