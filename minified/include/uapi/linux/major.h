/* Minimal major.h - only keep numbers actually used */
#ifndef _LINUX_MAJOR_H
#define _LINUX_MAJOR_H

#define UNNAMED_MAJOR		0
#define MEM_MAJOR		1
#define RAMDISK_MAJOR		1
#define TTY_MAJOR		4
#define TTYAUX_MAJOR		5
#define MISC_MAJOR		10

/* Unused but needed by root_dev.h enum (values don't matter since unused) */
#define FLOPPY_MAJOR		2
#define IDE0_MAJOR		3
#define IDE1_MAJOR		22
#define SCSI_DISK0_MAJOR	8
#define SCSI_CDROM_MAJOR	11

#endif
