 
#include <linux/fs.h>
#include <linux/export.h>

 

unsigned char fs_ftype_to_dtype(unsigned int filetype)
{
	return DT_UNKNOWN;
}

unsigned char fs_umode_to_ftype(umode_t mode)
{
	return FT_UNKNOWN;
}

unsigned char fs_umode_to_dtype(umode_t mode)
{
	return DT_UNKNOWN;
}
