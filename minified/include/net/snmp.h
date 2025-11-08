#ifndef _NET_SNMP_H
#define _NET_SNMP_H

#include <linux/atomic.h>

/* SNMP MIB statistics macros - stubbed for minimal kernel */

#define SNMP_INC_STATS64(mib, field) do { (mib)->field++; } while (0)
#define SNMP_INC_STATS(mib, field) do { (mib)->field++; } while (0)
#define SNMP_INC_STATS_ATOMIC_LONG(mib, field) do { atomic_long_inc(&(mib)->field); } while (0)
#define SNMP_ADD_STATS(mib, field, val) do { (mib)->field += (val); } while (0)
#define SNMP_UPD_PO_STATS(mib, field, val) do { (mib)->field += (val); } while (0)

#endif /* _NET_SNMP_H */