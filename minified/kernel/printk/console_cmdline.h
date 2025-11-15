 
#ifndef _CONSOLE_CMDLINE_H
#define _CONSOLE_CMDLINE_H

struct console_cmdline
{
	char	name[16];			 
	int	index;				 
	bool	user_specified;			 
	char	*options;			 
};

#endif
