#ifndef _LINUX_EXPORT_H
#define _LINUX_EXPORT_H

#include <linux/stringify.h>



#ifndef __ASSEMBLY__
/* Built-in kernel: MODULE not defined */
#define THIS_MODULE ((struct module *)0)

#include <linux/compiler.h>
/* __KSYMTAB_ENTRY, ___EXPORT_SYMBOL removed - no modules, EXPORT_SYMBOL never used */

#define __EXPORT_SYMBOL(sym, sec, ns)


/* DEFAULT_SYMBOL_NAMESPACE not defined */
#define _EXPORT_SYMBOL(sym, sec)	__EXPORT_SYMBOL(sym, sec, "")

#define EXPORT_SYMBOL(sym)		_EXPORT_SYMBOL(sym, "")
#define EXPORT_SYMBOL_GPL(sym)		_EXPORT_SYMBOL(sym, "_gpl")
/* EXPORT_SYMBOL_NS, EXPORT_SYMBOL_NS_GPL removed - unused */

#endif  

#endif  
