/* The functions of wasi-shim.h (see there). */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "wasi-shim.h"

#undef close

#define MAX_MAPS 16

static struct map {
	char *addr;
	size_t len;
	int fd;
	int shared_write;
	int close_pending;
} maps[MAX_MAPS];

void *mkb_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	struct map *m = NULL;
	size_t got = 0;
	int i;

	(void)addr;
	for (i = 0; i < MAX_MAPS; i++)
		if (!maps[i].addr) {
			m = &maps[i];
			break;
		}
	if (!m || !len) {
		errno = m ? EINVAL : ENOMEM;
		return MAP_FAILED;
	}
	m->addr = malloc(len);
	if (!m->addr) {
		errno = ENOMEM;
		return MAP_FAILED;
	}
	while (got < len) {
		ssize_t n = pread(fd, m->addr + got, len - got, off + got);

		if (n < 0) {
			free(m->addr);
			m->addr = NULL;
			return MAP_FAILED;
		}
		if (n == 0)
			break;
		got += n;
	}
	memset(m->addr + got, 0, len - got);
	m->len = len;
	m->fd = fd;
	m->shared_write = (flags & MAP_SHARED) && (prot & PROT_WRITE);
	m->close_pending = 0;
	return m->addr;
}

int mkb_munmap(void *addr, size_t len)
{
	int i, rc = 0;

	for (i = 0; i < MAX_MAPS; i++) {
		struct map *m = &maps[i];
		size_t put = 0;

		if (m->addr != addr)
			continue;
		if (len != m->len) {
			errno = EINVAL;
			return -1;
		}
		while (m->shared_write && put < len) {
			ssize_t n = pwrite(m->fd, m->addr + put, len - put, put);

			if (n <= 0) {
				rc = -1;
				break;
			}
			put += n;
		}
		if (m->close_pending && close(m->fd))
			rc = -1;
		free(m->addr);
		m->addr = NULL;
		return rc;
	}
	errno = EINVAL;
	return -1;
}

int mkb_close(int fd)
{
	int i;

	for (i = 0; i < MAX_MAPS; i++)
		if (maps[i].addr && maps[i].fd == fd && maps[i].shared_write) {
			maps[i].close_pending = 1;
			return 0;
		}
	return close(fd);
}

int mkb_pthread_create(pthread_t *t, const pthread_attr_t *attr, void *(*fn)(void *), void *arg)
{
	void **ret = malloc(sizeof(*ret));

	(void)attr;
	if (!ret)
		return EAGAIN;
	*ret = fn(arg);
	memcpy(t, &ret, sizeof(ret) < sizeof(*t) ? sizeof(ret) : sizeof(*t));
	return 0;
}

int mkb_pthread_join(pthread_t t, void **retval)
{
	void **ret;

	memcpy(&ret, &t, sizeof(ret) < sizeof(t) ? sizeof(ret) : sizeof(t));
	if (retval)
		*retval = *ret;
	free(ret);
	return 0;
}

FILE *mkb_popen(const char *cmd, const char *mode)
{
	char name[256], file[256], line[1024], *out = NULL;
	size_t outlen = 0;
	FILE *in, *f;

	if (strcmp(mode, "r") ||
	    sscanf(cmd, " grep %255s %255s | awk '{print $1}' ", name, file) != 2) {
		errno = ENOSYS;
		return NULL;
	}
	in = fopen(file, "r");
	if (!in)
		return NULL;
	f = open_memstream(&out, &outlen);
	if (!f) {
		fclose(in);
		return NULL;
	}
	while (fgets(line, sizeof(line), in)) {
		size_t n = strcspn(line, " \t\n");

		if (strstr(line, name) && n)
			fprintf(f, "%.*s\n", (int)n, line);
	}
	fclose(in);
	fclose(f);
	f = fmemopen(out, outlen ? outlen : 1, "r");
	if (!outlen && f)
		fseek(f, 0, SEEK_END);
	/* out stays allocated while f is read; one popen per address. */
	return f;
}

int mkb_pclose(FILE *f)
{
	return fclose(f) ? -1 : 0;
}
