// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/export.h>

/* Minimal stub - file type conversions not used in minimal kernel */

unsigned char fs_ftype_to_dtype(unsigned int filetype)
{
	return DT_UNKNOWN;
}
EXPORT_SYMBOL_GPL(fs_ftype_to_dtype);

unsigned char fs_umode_to_ftype(umode_t mode)
{
	return FT_UNKNOWN;
}
EXPORT_SYMBOL_GPL(fs_umode_to_ftype);

unsigned char fs_umode_to_dtype(umode_t mode)
{
	return DT_UNKNOWN;
}
EXPORT_SYMBOL_GPL(fs_umode_to_dtype);
