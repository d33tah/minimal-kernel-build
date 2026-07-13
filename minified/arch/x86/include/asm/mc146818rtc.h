 
 
#ifndef _ASM_X86_MC146818RTC_H
#define _ASM_X86_MC146818RTC_H

#include <asm/io.h>
#include <asm/processor.h>

/* RTC_ALWAYS_BCD, cmos_lock + lock_cmos/unlock_cmos + lock_cmos_prefix/suffix,
 * CMOS_READ/rtc_cmos_read, mach_set_rtc_mmss/mach_get_cmos_time removed -
 * transitively 0-ref (lock_cmos/unlock_cmos only fed the dead prefix/suffix
 * macros; cmos_lock only fed those inlines; all protos uncalled). */


#endif  
