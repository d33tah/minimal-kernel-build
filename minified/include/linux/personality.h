#ifndef _LINUX_PERSONALITY_H
#define _LINUX_PERSONALITY_H

enum {
	ADDR_NO_RANDOMIZE = 	0x0040000,
	MMAP_PAGE_ZERO =	0x0100000,
	ADDR_COMPAT_LAYOUT =	0x0200000,
	READ_IMPLIES_EXEC =	0x0400000,
};

#define PER_CLEAR_ON_SETID (READ_IMPLIES_EXEC | ADDR_NO_RANDOMIZE | ADDR_COMPAT_LAYOUT | MMAP_PAGE_ZERO)

enum {
	PER_LINUX =		0x0000,
	PER_MASK =		0x00ff,
};

#define personality(pers)	(pers & PER_MASK)
#define set_personality(pers)	(current->personality = (pers))

#endif  
