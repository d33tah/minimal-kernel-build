#ifndef _LINUX_CTYPE_H
#define _LINUX_CTYPE_H

#define _U	0x01	 
#define _L	0x02	 
#define _D	0x04	 
#define _C	0x08	 
#define _P	0x10	 
#define _S	0x20	 
#define _X	0x40	 
#define _SP	0x80	 

extern const unsigned char _ctype[];

#define __ismask(x) (_ctype[(int)(unsigned char)(x)])

#define isalnum(c)	((__ismask(c)&(_U|_L|_D)) != 0)
/* isalpha, islower, isupper, isprint removed - unused */
#define isgraph(c)	((__ismask(c)&(_P|_U|_L|_D)) != 0)
#define isspace(c)	((__ismask(c)&(_S)) != 0)
#define isxdigit(c)	((__ismask(c)&(_D|_X)) != 0)

#if __has_builtin(__builtin_isdigit)
#define  isdigit(c) __builtin_isdigit(c)
#else
static inline int isdigit(int c)
{
	return '0' <= c && c <= '9';
}
#endif

/* __tolower, __toupper, tolower, toupper removed - unused */

static inline char _tolower(const char c)
{
	return c | 0x20;
}


#endif
