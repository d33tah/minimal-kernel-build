 
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/kernel_read_file.h>
#include <linux/vmalloc.h>

 

int kernel_read_file(struct file *file, loff_t offset, void **buf,
		     size_t buf_size, size_t *file_size,
		     enum kernel_read_file_id id)
{
	return -ENOSYS;
}

int kernel_read_file_from_path(const char *path, loff_t offset, void **buf,
			       size_t buf_size, size_t *file_size,
			       enum kernel_read_file_id id)
{
	return -ENOSYS;
}

int kernel_read_file_from_path_initns(const char *path, loff_t offset,
				      void **buf, size_t buf_size,
				      size_t *file_size,
				      enum kernel_read_file_id id)
{
	return -ENOSYS;
}

int kernel_read_file_from_fd(int fd, loff_t offset, void **buf,
			     size_t buf_size, size_t *file_size,
			     enum kernel_read_file_id id)
{
	return -ENOSYS;
}
