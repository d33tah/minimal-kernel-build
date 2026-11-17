// SPDX-License-Identifier: MIT
/*
 * kgzip: the gzip of the editable Debian kernel's build (KGZIP=kgzip,
 * plan E6). kbuild runs `cat FILES | $(KGZIP) -n -f -9 > OUT` for a
 * CONFIG_KERNEL_GZIP bzImage; this does the same with zlib (the release
 * gen-host-tools.py pins), stdin to stdout. The same source is built for
 * the build host and for wasm32-wasip1, so the page compresses to the
 * bytes CI's build wrote (zlib's deflate does not depend on the word
 * size). The gzip header has no name and time 0, as gzip -n writes it.
 *
 * Options: -n (always), -f (ignored: the output is never a terminal
 * here), -1 to -9 (level, default 6). Anything else is an error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define CHUNK (1 << 16)

int main(int argc, char **argv)
{
	static unsigned char in[CHUNK], out[CHUNK];
	int level = 6;
	z_stream s;
	int flush, r;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (a[0] != '-' || !a[1]) {
			fprintf(stderr, "kgzip: stdin to stdout only, not %s\n", a);
			return 2;
		}
		for (const char *c = a + 1; *c; c++) {
			if (*c >= '1' && *c <= '9')
				level = *c - '0';
			else if (*c != 'n' && *c != 'f') {
				fprintf(stderr, "kgzip: unknown option -%c\n", *c);
				return 2;
			}
		}
	}
	memset(&s, 0, sizeof(s));
	/* 15 + 16: a gzip wrapper with zlib's default header (no name, time 0). */
	if (deflateInit2(&s, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
		fprintf(stderr, "kgzip: deflateInit2 failed\n");
		return 1;
	}
	do {
		s.avail_in = fread(in, 1, CHUNK, stdin);
		if (ferror(stdin)) {
			perror("kgzip: stdin");
			return 1;
		}
		flush = feof(stdin) ? Z_FINISH : Z_NO_FLUSH;
		s.next_in = in;
		do {
			s.avail_out = CHUNK;
			s.next_out = out;
			r = deflate(&s, flush);
			if (r == Z_STREAM_ERROR) {
				fprintf(stderr, "kgzip: deflate failed\n");
				return 1;
			}
			if (fwrite(out, 1, CHUNK - s.avail_out, stdout) != CHUNK - s.avail_out) {
				perror("kgzip: stdout");
				return 1;
			}
		} while (s.avail_out == 0);
	} while (flush != Z_FINISH);
	deflateEnd(&s);
	if (fflush(stdout)) {
		perror("kgzip: stdout");
		return 1;
	}
	return 0;
}
