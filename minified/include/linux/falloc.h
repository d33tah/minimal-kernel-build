#ifndef _FALLOC_H_
#define _FALLOC_H_

#define FALLOC_FL_KEEP_SIZE	0x01
#define FALLOC_FL_PUNCH_HOLE	0x02
#define FALLOC_FL_NO_HIDE_STALE	0x04
#define FALLOC_FL_COLLAPSE_RANGE	0x08
#define FALLOC_FL_ZERO_RANGE		0x10
#define FALLOC_FL_INSERT_RANGE		0x20
#define FALLOC_FL_UNSHARE_RANGE		0x40

struct space_resv {
	__s16		l_type;
	__s16		l_whence;
	__s64		l_start;
	__s64		l_len;		 
	__s32		l_sysid;
	__u32		l_pid;
	__s32		l_pad[4];	 
};

#define FS_IOC_RESVSP		_IOW('X', 40, struct space_resv)
#define FS_IOC_UNRESVSP		_IOW('X', 41, struct space_resv)
#define FS_IOC_RESVSP64		_IOW('X', 42, struct space_resv)
#define FS_IOC_UNRESVSP64	_IOW('X', 43, struct space_resv)
#define FS_IOC_ZERO_RANGE	_IOW('X', 57, struct space_resv)

#define	FALLOC_FL_SUPPORTED_MASK	(FALLOC_FL_KEEP_SIZE |		\
					 FALLOC_FL_PUNCH_HOLE |		\
					 FALLOC_FL_COLLAPSE_RANGE |	\
					 FALLOC_FL_ZERO_RANGE |		\
					 FALLOC_FL_INSERT_RANGE |	\
					 FALLOC_FL_UNSHARE_RANGE)


#endif  
