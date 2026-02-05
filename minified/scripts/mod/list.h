/* Minimal list.h for modpost - only struct definition needed */
#ifndef LIST_H
#define LIST_H

struct list_head {
	struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#endif
