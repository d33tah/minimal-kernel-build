 
 

#ifndef __LOCKING_LOCK_EVENTS_H
#define __LOCKING_LOCK_EVENTS_H

enum lock_events {

#include "lock_events_list.h"

	lockevent_num,	 
	LOCKEVENT_reset_cnts = lockevent_num,
};


#define lockevent_inc(ev)
#define lockevent_add(ev, c)
#define lockevent_cond_inc(ev, c)

#endif  
