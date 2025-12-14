#ifndef _LINUX_HIGHUID_H
#define _LINUX_HIGHUID_H

#include <linux/types.h>




extern int overflowuid;
extern int overflowgid;

#define DEFAULT_OVERFLOWUID	65534
#define DEFAULT_OVERFLOWGID	65534


#define __convert_uid(size, uid) (uid)
#define __convert_gid(size, gid) (gid)


#define SET_UID(var, uid) do { (var) = __convert_uid(sizeof(var), (uid)); } while (0)
#define SET_GID(var, gid) do { (var) = __convert_gid(sizeof(var), (gid)); } while (0)


#define low_16_bits(x)	((x) & 0xFFFF)
#define high_16_bits(x)	(((x) & 0xFFFF0000) >> 16)

#endif  
