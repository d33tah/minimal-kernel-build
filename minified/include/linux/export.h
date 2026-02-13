#ifndef _LINUX_EXPORT_H
#define _LINUX_EXPORT_H

#include <linux/stringify.h>

#ifndef __ASSEMBLY__
#define THIS_MODULE ((struct module *)0)

#include <linux/compiler.h>

#define __EXPORT_SYMBOL(sym, sec, ns)

#define _EXPORT_SYMBOL(sym, sec)	__EXPORT_SYMBOL(sym, sec, "")

#define EXPORT_SYMBOL(sym)		_EXPORT_SYMBOL(sym, "")
#define EXPORT_SYMBOL_GPL(sym)		_EXPORT_SYMBOL(sym, "_gpl")

#endif  

#endif  
