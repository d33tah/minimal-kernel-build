/* relocs.h inlined */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <elf.h>
#include <byteswap.h>
#define USE_BSD
#include <endian.h>
#include <regex.h>
#include <tools/le_byteshift.h>
__attribute__((__format__(printf, 1, 2))) void die(char *fmt, ...)
	__attribute__((noreturn));
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
enum symtype { S_ABS, S_REL, S_SEG, S_LIN, S_NSYMTYPES };
void process_32(FILE *fp, int use_real_mode);

#define ELF_BITS 32

#define ELF_MACHINE EM_386
#define ELF_MACHINE_NAME "i386"
#define SHT_REL_TYPE SHT_REL
#define Elf_Rel ElfW(Rel)

#define ELF_CLASS ELFCLASS32
#define ELF_R_SYM(val) ELF32_R_SYM(val)
#define ELF_R_TYPE(val) ELF32_R_TYPE(val)
#define ELF_ST_TYPE(o) ELF32_ST_TYPE(o)
#define ELF_ST_BIND(o) ELF32_ST_BIND(o)
#define ELF_ST_VISIBILITY(o) ELF32_ST_VISIBILITY(o)

#include "relocs.c"
