 
 

#ifndef _LINUX_TBOOT_H
#define _LINUX_TBOOT_H

 
enum {
	TB_SHUTDOWN_REBOOT = 0,
	TB_SHUTDOWN_S5,
	TB_SHUTDOWN_S4,
	TB_SHUTDOWN_S3,
	TB_SHUTDOWN_HALT,
	TB_SHUTDOWN_WFS
};


#define tboot_enabled()			0
#define tboot_probe()			do { } while (0)
#define tboot_shutdown(shutdown_type)	do { } while (0)
#define tboot_sleep(sleep_state, pm1a_control, pm1b_control)	\
					do { } while (0)
#define tboot_get_dmar_table(dmar_tbl)	(dmar_tbl)
#define tboot_force_iommu()		0


#endif  
