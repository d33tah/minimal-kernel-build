
#ifndef BOOT_COMPRESSED_ERROR_H
#define BOOT_COMPRESSED_ERROR_H

void warn(char *m);
void error(char *m) __noreturn;

#endif
