#include "misc.h"

void warn(char *m)
{
	__putstr("\n\n");
	__putstr(m);
	__putstr("\n\n");
}

void error(char *m)
{
	warn(m);
	__putstr(" -- System halted");

	while (1)
		asm("hlt");
}
