

#include "boot.h"

int early_serial_base;

#define XMTRDY 0x20

#define TXR 0
#define LSR 5

static void __section(".inittext") serial_putchar(int ch)
{
	unsigned timeout = 0xffff;

	while ((inb(early_serial_base + LSR) & XMTRDY) == 0 && --timeout)
		cpu_relax();

	outb(ch, early_serial_base + TXR);
}

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

	if (early_serial_base != 0)
		serial_putchar(ch);
}

void __section(".inittext") puts(const char *str)
{
	while (*str)
		putchar(*str++);
}

int getchar(void)
{
	struct biosregs ireg, oreg;

	initregs(&ireg);

	intcall(0x16, &ireg, &oreg);

	return oreg.al;
}

