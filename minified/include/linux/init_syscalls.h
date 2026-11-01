/* SPDX-License-Identifier: GPL-2.0 */

int __init init_mknod(const char *filename, umode_t mode, unsigned int dev);
int __init init_mkdir(const char *pathname, umode_t mode);
int __init init_dup(struct file *file);
