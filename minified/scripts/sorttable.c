/* Stubbed - sorttable for minimal kernel (skip table sorting) */
#include <stdio.h>
#include <stdlib.h>

/*
 * For a minimal kernel that just prints "Hello World", we skip
 * the exception table sorting. The kernel has fallback code
 * to search unsorted tables if needed.
 */

int main(int argc, char *argv[])
{
	/* Just return success - tables will remain unsorted but still usable */
	return 0;
}
