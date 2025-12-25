#ifndef _LINUX_KERNEL_READ_FILE_H
#define _LINUX_KERNEL_READ_FILE_H

#include <linux/file.h>
#include <linux/types.h>

#define __kernel_read_file_id(id) \
	id(UNKNOWN, unknown)		\
	id(MAX_ID, )

#define __fid_enumify(ENUM, dummy) READING_ ## ENUM,

enum kernel_read_file_id {
	__kernel_read_file_id(__fid_enumify)
};

int kernel_read_file(struct file *file, loff_t offset,
		     void **buf, size_t buf_size,
		     size_t *file_size,
		     enum kernel_read_file_id id);
int kernel_read_file_from_path(const char *path, loff_t offset,
			       void **buf, size_t buf_size,
			       size_t *file_size,
			       enum kernel_read_file_id id);
int kernel_read_file_from_path_initns(const char *path, loff_t offset,
				      void **buf, size_t buf_size,
				      size_t *file_size,
				      enum kernel_read_file_id id);
int kernel_read_file_from_fd(int fd, loff_t offset,
			     void **buf, size_t buf_size,
			     size_t *file_size,
			     enum kernel_read_file_id id);

#endif  
