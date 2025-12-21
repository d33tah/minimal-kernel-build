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


static inline void dmi_setup(void) { }
#define dmi_available 0


#endif	 
