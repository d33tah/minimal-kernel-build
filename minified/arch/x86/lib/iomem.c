/* memcpy_fromio/memcpy_toio/memset_io are declared but never called
   Keep minimal stubs to satisfy linker if needed */
#include <linux/string.h>
#include <linux/io.h>

void memcpy_fromio(void *to, const volatile void __iomem *from, size_t n)
{
	memcpy(to, (const void __force *)from, n);
}

void memcpy_toio(volatile void __iomem *to, const void *from, size_t n)
{
	memcpy((void __force *)to, from, n);
}

void memset_io(volatile void __iomem *a, int b, size_t c)
{
	memset((void __force *)a, b, c);
}
