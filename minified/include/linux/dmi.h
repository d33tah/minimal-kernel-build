 
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


/* dmi_check_system, dmi_get_system_info, dmi_find_device removed - unused */
static inline void dmi_setup(void) { }
/* dmi_get_date, dmi_get_bios_year, dmi_name_in_vendors, dmi_name_in_serial removed - unused */
#define dmi_available 0
/* dmi_walk removed - unused */
static inline bool dmi_match(enum dmi_field f, const char *str)
	{ return false; }
/* dmi_memdev_*, dmi_first_match removed - unused */


#endif	 
