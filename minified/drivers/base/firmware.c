 
 

#include <linux/kobject.h>
#include <linux/export.h>

 
struct kobject *firmware_kobj;

 
int __init firmware_init(void)
{
	return 0;
}
