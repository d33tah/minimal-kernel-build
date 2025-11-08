/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Internet DSCP (Differentiated Services Code Point) definitions
 */

#ifndef _NET_INET_DSCP_H
#define _NET_INET_DSCP_H

#include <linux/types.h>

typedef u8 dscp_t;

static inline dscp_t inet_dsfield_to_dscp(__u8 dsfield)
{
	return dsfield >> 2;
}

#endif /* _NET_INET_DSCP_H */