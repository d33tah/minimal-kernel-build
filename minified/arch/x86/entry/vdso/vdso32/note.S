/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This supplies .note.* sections to go into the PT_NOTE inside the vDSO text.
 * Here we can supply some information useful to userland.
 */

/* Inlined from build-salt.h */
#include <linux/elfnote.h>

#define LINUX_ELFNOTE_BUILD_SALT       0x100

#ifdef __ASSEMBLER__

#define BUILD_SALT \
       ELFNOTE(Linux, LINUX_ELFNOTE_BUILD_SALT, .asciz CONFIG_BUILD_SALT)

#else

#define BUILD_SALT \
       ELFNOTE32("Linux", LINUX_ELFNOTE_BUILD_SALT, CONFIG_BUILD_SALT)

#endif
#include <linux/version.h>
#include <linux/elfnote.h>

/* Ideally this would use UTS_NAME, but using a quoted string here
   doesn't work. Remember to change this when changing the
   kernel's name. */
ELFNOTE_START(Linux, 0, "a")
	.long LINUX_VERSION_CODE
ELFNOTE_END

BUILD_SALT
