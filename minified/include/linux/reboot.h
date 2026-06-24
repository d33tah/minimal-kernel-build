#ifndef _LINUX_REBOOT_H
#define _LINUX_REBOOT_H


#include <linux/notifier.h>

extern void machine_restart(char *cmd);

extern void emergency_restart(void);

/* Inlined from asm/emergency-restart.h */
extern void machine_emergency_restart(void);

#endif  
