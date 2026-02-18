#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>

int main(int argc, char **argv)
{
	int opt;

	while ((opt = getopt(argc, argv, "ei:mnT:o:awENd:")) != -1) {
	}

	return 0;
}
