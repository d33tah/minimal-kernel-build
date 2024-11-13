/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * net busy poll support
 * Copyright(c) 2013 Intel Corporation.
 *
 * Author: Eliezer Tamir
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 */

#ifndef _LINUX_NET_BUSY_POLL_H
#define _LINUX_NET_BUSY_POLL_H

#include <linux/netdevice.h>
#include <linux/sched/clock.h>
#include <linux/sched/signal.h>
#include <net/ip.h>

/*		0 - Reserved to indicate value not set
 *     1..NR_CPUS - Reserved for sender_cpu
 *  NR_CPUS+1..~0 - Region available for NAPI IDs
 */
#define MIN_NAPI_ID ((unsigned int)(NR_CPUS + 1))

#define BUSY_POLL_BUDGET 8

static inline unsigned long net_busy_loop_on(void)
{
	return 0;
}

static inline bool sk_can_busy_loop(struct sock *sk)
{
	return false;
}


static inline unsigned long busy_loop_current_time(void)
{
	return 0;
}

/* in poll/select we use the global sysctl_net_ll_poll value */
static inline bool busy_loop_timeout(unsigned long start_time)
{
	return true;
}

static inline bool sk_busy_loop_timeout(struct sock *sk,
					unsigned long start_time)
{
	return true;
}

static inline void sk_busy_loop(struct sock *sk, int nonblock)
{
}

/* used in the NIC receive handler to mark the skb */
static inline void skb_mark_napi_id(struct sk_buff *skb,
				    struct napi_struct *napi)
{
}

/* used in the protocol hanlder to propagate the napi_id to the socket */
static inline void sk_mark_napi_id(struct sock *sk, const struct sk_buff *skb)
{
	sk_rx_queue_update(sk, skb);
}

/* Variant of sk_mark_napi_id() for passive flow setup,
 * as sk->sk_napi_id and sk->sk_rx_queue_mapping content
 * needs to be set.
 */
static inline void sk_mark_napi_id_set(struct sock *sk,
				       const struct sk_buff *skb)
{
	sk_rx_queue_set(sk, skb);
}

static inline void __sk_mark_napi_id_once(struct sock *sk, unsigned int napi_id)
{
}

/* variant used for unconnected sockets */
static inline void sk_mark_napi_id_once(struct sock *sk,
					const struct sk_buff *skb)
{
}

static inline void sk_mark_napi_id_once_xdp(struct sock *sk,
					    const struct xdp_buff *xdp)
{
}

#endif /* _LINUX_NET_BUSY_POLL_H */
