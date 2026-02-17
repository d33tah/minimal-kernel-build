
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#define NOFAIL(ptr)   do_nofail((ptr), #ptr)
void *do_nofail(void *ptr, const char *expr);

struct buffer {
	char *p;
	int pos;
	int size;
};

void __attribute__((format(printf, 2, 3)))
buf_printf(struct buffer *buf, const char *fmt, ...);
void buf_write(struct buffer *buf, const char *s, int len);

enum loglevel {
	LOG_WARN,
	LOG_ERROR,
	LOG_FATAL
};

void modpost_log(enum loglevel loglevel, const char *fmt, ...);

#define warn(fmt, args...)	modpost_log(LOG_WARN, fmt, ##args)
#define error(fmt, args...)	modpost_log(LOG_ERROR, fmt, ##args)
#define fatal(fmt, args...)	modpost_log(LOG_FATAL, fmt, ##args)
