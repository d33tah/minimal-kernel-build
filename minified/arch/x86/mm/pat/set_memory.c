#include <linux/types.h>
#include <linux/mm_types.h>

int set_memory_x(unsigned long addr, int numpages)
{
	return 0;
}
int set_memory_nx(unsigned long addr, int numpages)
{
	return 0;
}
int set_memory_ro(unsigned long addr, int numpages)
{
	return 0;
}
int set_memory_rw(unsigned long addr, int numpages)
{
	return 0;
}
int set_memory_np(unsigned long addr, int numpages)
{
	return 0;
}
int set_memory_encrypted(unsigned long addr, int numpages)
{
	return 0;
}
int set_memory_decrypted(unsigned long addr, int numpages)
{
	return 0;
}
int set_pages_ro(struct page *page, int numpages)
{
	return 0;
}
int set_direct_map_invalid_noflush(struct page *page)
{
	return 0;
}
int set_direct_map_default_noflush(struct page *page)
{
	return 0;
}
