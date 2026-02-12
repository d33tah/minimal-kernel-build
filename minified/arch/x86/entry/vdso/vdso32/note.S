/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This supplies .note.* sections to go into the PT_NOTE inside the vDSO text.
 * Here we can supply some information useful to userland.
 */

/* --- Inlined from linux/elfnote.h (asm branch) --- */
#define ELFNOTE_START(name, type, flags)	\
.pushsection .note.name, flags,@note	;	\
  .balign 4				;	\
  .long 2f - 1f		 	;	\
  .long 4484f - 3f	 	;	\
  .long type				;	\
1:.asciz #name				;	\
2:.balign 4				;	\
3:

#define ELFNOTE_END				\
4484:.balign 4				;	\
.popsection				;

#define ELFNOTE(name, type, desc)		\
	ELFNOTE_START(name, type, "a")		\
		desc			;	\
	ELFNOTE_END
/* end elfnote.h */

/* Inlined from build-salt.h */
#define LINUX_ELFNOTE_BUILD_SALT       0x100
#define BUILD_SALT \
       ELFNOTE(Linux, LINUX_ELFNOTE_BUILD_SALT, .asciz CONFIG_BUILD_SALT)
/* end build-salt.h */

#include <linux/version.h>

ELFNOTE_START(Linux, 0, "a")
	.long LINUX_VERSION_CODE
ELFNOTE_END

BUILD_SALT
