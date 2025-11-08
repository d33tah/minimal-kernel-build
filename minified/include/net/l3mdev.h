/* SPDX-License-Identifier: GPL-2.0 */
/* Stub for L3 device management - networking disabled */
#ifndef _NET_L3MDEV_H
#define _NET_L3MDEV_H

/* Stub definitions for L3 device management */
struct l3mdev_ops {
	int (*l3mdev_fib_table)(const struct net_device *dev);
};

static inline int l3mdev_fib_table(const struct net_device *dev)
{
	return 0; /* Stubbed */
}

#endif /* _NET_L3MDEV_H */