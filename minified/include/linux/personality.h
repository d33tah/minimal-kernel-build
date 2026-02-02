#ifndef _LINUX_PERSONALITY_H
#define _LINUX_PERSONALITY_H

enum {
	/* ADDR_NO_RANDOMIZE removed - unused */
	MMAP_PAGE_ZERO =	0x0100000,
	ADDR_COMPAT_LAYOUT =	0x0200000,
	READ_IMPLIES_EXEC =	0x0400000,
};

/* PER_CLEAR_ON_SETID removed - never used */

enum {
	PER_LINUX =		0x0000,
	PER_MASK =		0x00ff,
};

#define personality(pers)	(pers & PER_MASK)
#define set_personality(pers)	(current->personality = (pers))

#endif  
