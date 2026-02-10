#include "misc.h"
/* error.h inlined */
void warn(char *m);
void error(char *m) __noreturn;
void warn(char *m)
{
	error_putstr("\n\n");
	error_putstr(m);
	error_putstr("\n\n");
}
void error(char *m)
{
	warn(m);
	error_putstr(" -- System halted");
	while (1)
		asm("hlt");
}
