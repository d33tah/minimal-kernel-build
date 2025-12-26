#ifndef _LINUX_EDD_H
#define _LINUX_EDD_H

#include <linux/types.h>
#define EDDMAXNR 6
#define EDD_MBR_SIG_MAX 16
#ifndef __ASSEMBLY__

struct edd_device_params {
	__u16 length;
	__u16 info_flags;
	__u32 num_default_cylinders;
	__u32 num_default_heads;
	__u32 sectors_per_track;
	__u64 number_of_sectors;
	__u16 bytes_per_sector;
	__u32 dpte_ptr;
	__u16 key;
	__u8 device_path_info_length;
	__u8 reserved2;
	__u16 reserved3;
	__u8 host_bus_type[4];
	__u8 interface_type[8];
	__u8 interface_path[8];
	__u8 device_path[16];
	__u8 reserved4;
	__u8 checksum;
} __attribute__ ((packed));

struct edd_info {
	__u8 device;
	__u8 version;
	__u16 interface_support;
	__u16 legacy_max_cylinder;
	__u8 legacy_max_head;
	__u8 legacy_sectors_per_track;
	struct edd_device_params params;
} __attribute__ ((packed));

struct edd {
	unsigned int mbr_signature[EDD_MBR_SIG_MAX];
	struct edd_info edd_info[EDDMAXNR];
	unsigned char mbr_signature_nr;
	unsigned char edd_info_nr;
};

extern struct edd edd;
#endif
#endif
