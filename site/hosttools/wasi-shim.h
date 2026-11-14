/*
 * Included (-include) into every kernel build host tool compiled for
 * wasm32-wasip1 by gen-host-tools.sh: what the tools use that WASI
 * preview1 and wasi-libc do not have. The tools' sources stay as they are.
 *
 * - mmap/munmap: the file is read into memory; a writable MAP_SHARED
 *   mapping is written back to its file at munmap (sorttable sorts
 *   vmlinux in place that way). close() of that file waits for the
 *   munmap, since sorttable closes the descriptor first.
 * - pthread_create/pthread_join: the thread function runs to its end
 *   inside pthread_create (sorttable sorts __mcount_loc in a thread).
 * - popen/pclose: only the command sorttable runs to read two addresses
 *   from System.map, " grep NAME FILE | awk '{print $1}' ", done here.
 */
#ifndef MKB_WASI_SHIM_H
#define MKB_WASI_SHIM_H

/* wasi-libc's <sys/mman.h> (its constants; the functions are below). */
#ifndef _WASI_EMULATED_MMAN
#define _WASI_EMULATED_MMAN
#endif
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <pthread.h>
#include <unistd.h>

void *mkb_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
int mkb_munmap(void *addr, size_t len);
int mkb_close(int fd);
int mkb_pthread_create(pthread_t *t, const pthread_attr_t *attr, void *(*fn)(void *), void *arg);
int mkb_pthread_join(pthread_t t, void **ret);
FILE *mkb_popen(const char *cmd, const char *mode);
int mkb_pclose(FILE *f);

#define mmap mkb_mmap
#define munmap mkb_munmap
#define close mkb_close
#define pthread_create mkb_pthread_create
#define pthread_join mkb_pthread_join
#define popen mkb_popen
#define pclose mkb_pclose

#endif
