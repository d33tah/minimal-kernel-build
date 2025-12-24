/* memremap/memunmap stubbed - never called (only early_memremap is used) */
#include <linux/io.h>
#include <linux/types.h>
void *memremap(resource_size_t offset, size_t size, unsigned long flags)
{
	return NULL;
}
void memunmap(void *addr)
{
}
