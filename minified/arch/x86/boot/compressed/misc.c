// SPDX-License-Identifier: GPL-2.0
/*
 * misc.c
 *
 * This is a collection of several routines used to extract the kernel
 * which includes KASLR relocation, decompression, ELF parsing, and
 * relocation processing. Additionally included are the screen and serial
 * output functions and related debugging support functions.
 *
 * malloc by Hannu Savolainen 1993 and Matthias Urlichs 1994
 * puts by Nick Holloway 1993, better puts by Martin Mares 1995
 * High loaded stuff by Hans Lermen & Werner Almesberger, Feb. 1996
 */

#include "misc.h"
#include "error.h"
#include <linux/kernel.h>
#include <asm/bootparam_utils.h>

/* Functions used by the included decompressor code below. */
void *memmove(void *dest, const void *src, size_t n);

/*
 * This is set up by the setup-routine at boot-time
 */
struct boot_params *boot_params;

struct port_io_ops pio_ops;

static char *vidmem;
static int vidport;

/* These might be accessed before .bss is cleared, so use .data instead. */
static int lines __section(".data");
static int cols __section(".data");

/*
 * NOTE: When adding a new decompressor, please update the analysis in
 * ../header.S.
 */

static void scroll(void)
{
	int i;

	memmove(vidmem, vidmem + cols * 2, (lines - 1) * cols * 2);
	for (i = (lines - 1) * cols * 2; i < lines * cols * 2; i += 2)
		vidmem[i] = ' ';
}

void __putstr(const char *s)
{
	int x, y, pos;
	char c;

	if (lines == 0 || cols == 0)
		return;

	x = boot_params->screen_info.orig_x;
	y = boot_params->screen_info.orig_y;

	while ((c = *s++) != '\0') {
		if (c == '\n') {
			x = 0;
			if (++y >= lines) {
				scroll();
				y--;
			}
		} else {
			vidmem[(x + cols * y) * 2] = c;
			if (++x >= cols) {
				x = 0;
				if (++y >= lines) {
					scroll();
					y--;
				}
			}
		}
	}

	boot_params->screen_info.orig_x = x;
	boot_params->screen_info.orig_y = y;

	pos = (x + cols * y) * 2; /* Update cursor position */
	outb(14, vidport);
	outb(0xff & (pos >> 9), vidport + 1);
	outb(15, vidport);
	outb(0xff & (pos >> 1), vidport + 1);
}

/*
 * The payload (vmlinux.bin) is the kernel flattened with objcopy -O binary
 * at its load address LOAD_PHYSICAL_ADDR: copying it to "output" (which head_32.S
 * set to LOAD_PHYSICAL_ADDR; the kernel is not relocatable) places every
 * section; the kernel's own startup_32 clears .bss.
 */
asmlinkage __visible void *
extract_kernel(void *rmode, unsigned char *input_data, unsigned long input_len,
	       unsigned char *output, unsigned long output_len)
{
	/* Retain x86 boot parameters pointer passed from startup_32/64. */
	boot_params = rmode;

	sanitize_boot_params(boot_params);

	if (boot_params->screen_info.orig_video_mode == 7) {
		vidmem = (char *)0xb0000;
		vidport = 0x3b4;
	} else {
		vidmem = (char *)0xb8000;
		vidport = 0x3d4;
	}

	lines = boot_params->screen_info.orig_video_lines;
	cols = boot_params->screen_info.orig_video_cols;

	init_default_io_ops();

	/* Validate memory location choices. */
	if ((unsigned long)output & (MIN_KERNEL_ALIGN - 1))
		error("Destination physical address inappropriately aligned");

	if (input_len < output_len)
		error("payload shorter than kernel");
	memmove(output, input_data, output_len);

	return output;
}
