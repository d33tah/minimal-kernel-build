#ifndef _STATIC_CALL_TYPES_H
#define _STATIC_CALL_TYPES_H

/* struct static_call_key + STATIC_CALL_KEY/TRAMP/DECLARE_STATIC_CALL/static_call
   machinery removed - no DECLARE_STATIC_CALL or static_call() sites exist
   tree-wide and the arch trampoline macros were already gutted, so the whole
   cluster was a closed dead loop. */

#endif
