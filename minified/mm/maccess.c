#include <linux/export.h>
#include <linux/mm.h>
#include <linux/uaccess.h>

/* copy_from_kernel_nofault_allowed provided by arch/x86/mm/maccess.c */

#define copy_from_kernel_nofault_loop(dst, src, len, type, err_label) \
	while (len >= sizeof(type)) {                                 \
		__get_kernel_nofault(dst, src, type, err_label);      \
		dst += sizeof(type);                                  \
		src += sizeof(type);                                  \
		len -= sizeof(type);                                  \
	}

/* CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS is enabled - use direct 64-bit access */
long copy_from_kernel_nofault(void *dst, const void *src, size_t size)
{
	if (!copy_from_kernel_nofault_allowed(src, size))
		return -ERANGE;

	pagefault_disable();
	copy_from_kernel_nofault_loop(dst, src, size, u64, Efault);
	copy_from_kernel_nofault_loop(dst, src, size, u32, Efault);
	copy_from_kernel_nofault_loop(dst, src, size, u16, Efault);
	copy_from_kernel_nofault_loop(dst, src, size, u8, Efault);
	pagefault_enable();
	return 0;
Efault:
	pagefault_enable();
	return -EFAULT;
}

#define copy_to_kernel_nofault_loop(dst, src, len, type, err_label) \
	while (len >= sizeof(type)) {                               \
		__put_kernel_nofault(dst, src, type, err_label);    \
		dst += sizeof(type);                                \
		src += sizeof(type);                                \
		len -= sizeof(type);                                \
	}

long copy_to_kernel_nofault(void *dst, const void *src, size_t size)
{
	pagefault_disable();
	copy_to_kernel_nofault_loop(dst, src, size, u64, Efault);
	copy_to_kernel_nofault_loop(dst, src, size, u32, Efault);
	copy_to_kernel_nofault_loop(dst, src, size, u16, Efault);
	copy_to_kernel_nofault_loop(dst, src, size, u8, Efault);
	pagefault_enable();
	return 0;
Efault:
	pagefault_enable();
	return -EFAULT;
}

/* Removed uncalled functions:
 * strncpy_from_kernel_nofault, copy_from_user_nofault, copy_to_user_nofault,
 * strncpy_from_user_nofault, strnlen_user_nofault, __copy_overflow */
