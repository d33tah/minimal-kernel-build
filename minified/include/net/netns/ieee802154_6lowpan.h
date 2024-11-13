/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ieee802154 6lowpan in net namespaces
 */

#include <net/inet_frag.h>

#ifndef __NETNS_IEEE802154_6LOWPAN_H__
#define __NETNS_IEEE802154_6LOWPAN_H__

struct netns_sysctl_lowpan {
};

struct netns_ieee802154_lowpan {
	struct netns_sysctl_lowpan sysctl;
	struct fqdir		*fqdir;
};

#endif
