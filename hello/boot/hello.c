/* The whole "kernel": print two lines on the VGA text console and halt. */

#define VGA_TEXT ((volatile unsigned short *)0xb8000)
#define VGA_COLS 80
#define VGA_ROWS 25
#define ATTR 0x07 /* light grey on black */

void hello_main(void);

static void put_line(int row, const char *s)
{
	volatile unsigned short *p = VGA_TEXT + row * VGA_COLS;

	while (*s)
		*p++ = ATTR << 8 | (unsigned char)*s++;
}

void hello_main(void)
{
	int i;

	for (i = 0; i < VGA_COLS * VGA_ROWS; i++)
		VGA_TEXT[i] = ATTR << 8 | ' ';
	put_line(0, "Hello, World!");
	put_line(1, "Still alive");
	for (;;)
		__asm__ volatile("cli; hlt");
}
