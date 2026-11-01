/* The whole "kernel": print two lines on the VGA text console and halt. */

#define VGA_TEXT ((volatile unsigned short *)0xb8000)
#define VGA_COLS 80
#define VGA_ROWS 25
#define ATTR 0x07 /* light grey on black */
/* BIOS data area: cursor row of video page 0 (what the BIOS printed last). */
#define BDA_CURSOR_ROW (*(volatile unsigned char *)0x451)

void hello_main(void);

static void put_line(int row, const char *s)
{
	volatile unsigned short *p = VGA_TEXT + row * VGA_COLS;

	while (*s)
		*p++ = ATTR << 8 | (unsigned char)*s++;
}

void hello_main(void)
{
	/*
	 * No screen clear: like Linux's vgacon, continue below what the BIOS
	 * printed, at the BIOS cursor row (clearing 2,000 cells was most of
	 * the instructions this kernel ran).
	 */
	int row = BDA_CURSOR_ROW;

	if (row > VGA_ROWS - 2)
		row = VGA_ROWS - 2;
	put_line(row, "Hello, World!");
	put_line(row + 1, "Still alive");
	for (;;)
		__asm__ volatile("cli; hlt");
}
