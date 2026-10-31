#ifndef _LINUX_EDD_H
#define _LINUX_EDD_H

#define EDDMAXNR 6
#define EDD_MBR_SIG_MAX 16

struct edd_device_params {
	__u16 length;
	__u16 info_flags;
	__u32 num_default_cylinders;
	__u32 num_default_heads;
	__u32 sectors_per_track;
	__u64 number_of_sectors;
	__u16 bytes_per_sector;
	__u32 dpte_ptr; /* 0xFFFFFFFF for our purposes */
	__u16 key; /* = 0xBEDD */
	__u8 device_path_info_length; /* = 44 */
	__u8 reserved2;
	__u16 reserved3;
	__u8 host_bus_type[4];
	__u8 interface_type[8];
	union {
		struct {
			__u16 base_address;
			__u16 reserved1;
			__u32 reserved2;
		} __attribute__((packed)) isa;
		struct {
			__u8 bus;
			__u8 slot;
			__u8 function;
			__u8 channel;
			__u32 reserved;
		} __attribute__((packed)) pci;
		/* pcix is same as pci */
		struct {
			__u64 reserved;
		} __attribute__((packed)) ibnd;
		struct {
			__u64 reserved;
		} __attribute__((packed)) xprs;
		struct {
			__u64 reserved;
		} __attribute__((packed)) htpt;
		struct {
			__u64 reserved;
		} __attribute__((packed)) unknown;
	} interface_path;
	union {
		struct {
			__u8 device;
			__u8 reserved1;
			__u16 reserved2;
			__u32 reserved3;
			__u64 reserved4;
		} __attribute__((packed)) ata;
		struct {
			__u8 device;
			__u8 lun;
			__u8 reserved1;
			__u8 reserved2;
			__u32 reserved3;
			__u64 reserved4;
		} __attribute__((packed)) atapi;
		struct {
			__u16 id;
			__u64 lun;
			__u16 reserved1;
			__u32 reserved2;
		} __attribute__((packed)) scsi;
		struct {
			__u64 serial_number;
			__u64 reserved;
		} __attribute__((packed)) usb;
		struct {
			__u64 eui;
			__u64 reserved;
		} __attribute__((packed)) i1394;
		struct {
			__u64 wwid;
			__u64 lun;
		} __attribute__((packed)) fibre;
		struct {
			__u64 identity_tag;
			__u64 reserved;
		} __attribute__((packed)) i2o;
		struct {
			__u32 array_number;
			__u32 reserved1;
			__u64 reserved2;
		} __attribute__((packed)) raid;
		struct {
			__u8 device;
			__u8 reserved1;
			__u16 reserved2;
			__u32 reserved3;
			__u64 reserved4;
		} __attribute__((packed)) sata;
		struct {
			__u64 reserved1;
			__u64 reserved2;
		} __attribute__((packed)) unknown;
	} device_path;
	__u8 reserved4;
	__u8 checksum;
} __attribute__((packed));

struct edd_info {
	__u8 device;
	__u8 version;
	__u16 interface_support;
	__u16 legacy_max_cylinder;
	__u8 legacy_max_head;
	__u8 legacy_sectors_per_track;
	struct edd_device_params params;
} __attribute__((packed));

#endif
