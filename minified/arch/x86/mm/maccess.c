 

#include <linux/uaccess.h>
#include <linux/kernel.h>

bool copy_from_kernel_nofault_allowed(const void *unsafe_src, size_t size)
{
	return (unsigned long)unsafe_src >= TASK_SIZE_MAX;
}
