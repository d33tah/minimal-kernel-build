/* Stub for net/if_inet6.h */
#ifndef _NET_IF_INET6_H
#define _NET_IF_INET6_H

#include <linux/refcount.h>

struct inet6_ifaddr {
    refcount_t refcnt;
    int dummy;
};

struct inet6_dev {
    refcount_t refcnt;
    struct {
        int ignore_routes_with_linkdown;
        int forwarding;
        int accept_ra;
    } cnf;
    int dummy;
};

#endif /* _NET_IF_INET6_H */