#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <asm/setup.h>

static inline int myisspace(u8 c)
{
	return c <= ' ';
}

int cmdline_find_option_bool(const char *cmdline, const char *option)
{
	char c;
	int pos = 0, wstart = 0;
	const char *opptr = NULL;
	enum { st_wordstart = 0, st_wordcmp, st_wordskip } state = st_wordstart;

	if (!cmdline)
		return -1;

	while (pos < COMMAND_LINE_SIZE) {
		c = *(char *)cmdline++;
		pos++;

		switch (state) {
		case st_wordstart:
			if (!c)
				return 0;
			else if (myisspace(c))
				break;
			state = st_wordcmp;
			opptr = option;
			wstart = pos;
			fallthrough;
		case st_wordcmp:
			if (!*opptr) {
				if (!c || myisspace(c))
					return wstart;
			} else if (!c) {
				return 0;
			} else if (c == *opptr++) {
				break;
			}
			state = st_wordskip;
			fallthrough;
		case st_wordskip:
			if (!c)
				return 0;
			else if (myisspace(c))
				state = st_wordstart;
			break;
		}
	}
	return 0;
}

int cmdline_find_option(const char *cmdline, const char *option, char *buffer,
			int bufsize)
{
	char c;
	int pos = 0, len = -1;
	const char *opptr = NULL;
	char *bufptr = buffer;
	enum {
		st_wordstart = 0,
		st_wordcmp,
		st_wordskip,
		st_bufcpy
	} state = st_wordstart;

	if (!cmdline)
		return -1;

	while (pos++ < COMMAND_LINE_SIZE) {
		c = *(char *)cmdline++;
		if (!c)
			break;

		switch (state) {
		case st_wordstart:
			if (myisspace(c))
				break;
			state = st_wordcmp;
			opptr = option;
			fallthrough;
		case st_wordcmp:
			if ((c == '=') && !*opptr) {
				len = 0;
				bufptr = buffer;
				state = st_bufcpy;
				break;
			} else if (c == *opptr++) {
				break;
			}
			state = st_wordskip;
			fallthrough;
		case st_wordskip:
			if (myisspace(c))
				state = st_wordstart;
			break;
		case st_bufcpy:
			if (myisspace(c)) {
				state = st_wordstart;
			} else {
				if (++len < bufsize)
					*bufptr++ = c;
			}
			break;
		}
	}

	if (bufsize)
		*bufptr = '\0';

	return len;
}
