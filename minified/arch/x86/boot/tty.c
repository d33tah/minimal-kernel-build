
#include "boot.h"

static void __section(".inittext") bios_putchar(int ch)
{
	struct biosregs ireg;

	initregs(&ireg);
	ireg.bx = 0x0007;
	ireg.cx = 0x0001;
	ireg.ah = 0x0e;
	ireg.al = ch;
	intcall(0x10, &ireg, NULL);
}

void __section(".inittext") putchar(int ch)
{
	if (ch == '\n')
		putchar('\r');

	bios_putchar(ch);
}

void __section(".inittext") puts(const char *str)
{
	while (*str)
		putchar(*str++);
}
