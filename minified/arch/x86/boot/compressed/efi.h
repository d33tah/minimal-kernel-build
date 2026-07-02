 
#ifndef BOOT_COMPRESSED_EFI_H
#define BOOT_COMPRESSED_EFI_H

#if defined(_LINUX_EFI_H) || defined(_ASM_X86_EFI_H)
#error Please do not include kernel proper namespace headers
#endif

typedef guid_t efi_guid_t __aligned(__alignof__(u32));



#define EFI32_LOADER_SIGNATURE	"EL32"
#define EFI64_LOADER_SIGNATURE	"EL64"

 
typedef	struct {
	u64 signature;
	u32 revision;
	u32 headersize;
	u32 crc32;
	u32 reserved;
} efi_table_hdr_t;




typedef struct {
	u32 type;
	u32 pad;
	u64 phys_addr;
	u64 virt_addr;
	u64 num_pages;
	u64 attribute;
} efi_memory_desc_t;


typedef struct {
	efi_guid_t guid;
	u64 table;
} efi_config_table_64_t;

typedef struct {
	efi_guid_t guid;
	u32 table;
} efi_config_table_32_t;

typedef struct {
	efi_table_hdr_t hdr;
	u64 fw_vendor;	 
	u32 fw_revision;
	u32 __pad1;
	u64 con_in_handle;
	u64 con_in;
	u64 con_out_handle;
	u64 con_out;
	u64 stderr_handle;
	u64 stderr;
	u64 runtime;
	u64 boottime;
	u32 nr_tables;
	u32 __pad2;
	u64 tables;
} efi_system_table_64_t;

typedef struct {
	efi_table_hdr_t hdr;
	u32 fw_vendor;	 
	u32 fw_revision;
	u32 con_in_handle;
	u32 con_in;
	u32 con_out_handle;
	u32 con_out;
	u32 stderr_handle;
	u32 stderr;
	u32 runtime;
	u32 boottime;
	u32 nr_tables;
	u32 tables;
} efi_system_table_32_t;

#endif  
