 
#ifndef _LINUX_NTP_INTERNAL_H
#define _LINUX_NTP_INTERNAL_H

extern void ntp_init(void);
extern void ntp_clear(void);
 
extern u64 ntp_tick_length(void);
extern ktime_t ntp_get_next_leap(void);
extern int second_overflow(time64_t secs);

#endif
