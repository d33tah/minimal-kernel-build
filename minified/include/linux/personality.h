#ifndef _LINUX_PERSONALITY_H
#define _LINUX_PERSONALITY_H

enum {
	READ_IMPLIES_EXEC =	0x0400000,
};

enum {
	PER_LINUX =		0x0000,
	PER_MASK =		0x00ff,
};

#define personality(pers)	(pers & PER_MASK)

#endif  
