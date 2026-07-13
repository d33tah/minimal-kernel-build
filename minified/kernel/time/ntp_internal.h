 
#ifndef _LINUX_NTP_INTERNAL_H
#define _LINUX_NTP_INTERNAL_H

/* NTP stub: the real NTP engine was stripped; tick length is constant. */
static inline u64 ntp_tick_length(void) { return (u64)TICK_NSEC << 32; }

#endif
