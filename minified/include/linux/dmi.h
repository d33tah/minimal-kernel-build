 
#ifndef __DMI_H__
#define __DMI_H__

#include <linux/list.h>
#include <linux/kobject.h>
#include <linux/mod_devicetable.h>

 

enum dmi_device_type {
	DMI_DEV_TYPE_ANY = 0,
};

enum dmi_entry_type {
	DMI_ENTRY_BIOS = 0,
};

struct dmi_header {
	u8 type;
	u8 length;
	u16 handle;
} __packed;

struct dmi_device {
	struct list_head list;
	int type;
	const char *name;
	void *device_data;	 
};


static inline int dmi_check_system(const struct dmi_system_id *list) { return 0; }
static inline const char * dmi_get_system_info(int field) { return NULL; }
static inline const struct dmi_device * dmi_find_device(int type, const char *name,
	const struct dmi_device *from) { return NULL; }
static inline void dmi_setup(void) { }
static inline bool dmi_get_date(int field, int *yearp, int *monthp, int *dayp)
{
	if (yearp)
		*yearp = 0;
	if (monthp)
		*monthp = 0;
	if (dayp)
		*dayp = 0;
	return false;
}
static inline int dmi_get_bios_year(void) { return -ENXIO; }
static inline int dmi_name_in_vendors(const char *s) { return 0; }
static inline int dmi_name_in_serial(const char *s) { return 0; }
#define dmi_available 0
static inline int dmi_walk(void (*decode)(const struct dmi_header *, void *),
	void *private_data) { return -ENXIO; }
static inline bool dmi_match(enum dmi_field f, const char *str)
	{ return false; }
static inline void dmi_memdev_name(u16 handle, const char **bank,
		const char **device) { }
static inline u64 dmi_memdev_size(u16 handle) { return ~0ul; }
static inline u8 dmi_memdev_type(u16 handle) { return 0x0; }
static inline u16 dmi_memdev_handle(int slot) { return 0xffff; }
static inline const struct dmi_system_id *
	dmi_first_match(const struct dmi_system_id *list) { return NULL; }


#endif	 
