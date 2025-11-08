#ifndef _NET_INET_SOCK_H
#define _NET_INET_SOCK_H

#include <linux/sock.h>

struct inet_cork {
	int dummy; /* Minimal stub for inet_cork */
};

struct inet_cork_full {
	int dummy; /* Minimal stub for inet_cork_full */
};

struct inet_sock {
	struct sock sk;
	struct inet_cork cork;
	__be16 inet_num;
};

#define inet_sk(sk) ((struct inet_sock *)sk)

#endif /* _NET_INET_SOCK_H */