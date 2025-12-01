#ifndef _LINUX_EXPORT_H
#define _LINUX_EXPORT_H

#include <linux/stringify.h>



#ifndef __ASSEMBLY__
#ifdef MODULE
extern struct module __this_module;
#define THIS_MODULE (&__this_module)
#else
#define THIS_MODULE ((struct module *)0)
#endif

#include <linux/compiler.h>
#define __KSYMTAB_ENTRY(sym, sec)					\
	__ADDRESSABLE(sym)						\
	asm("	.section \"___ksymtab" sec "+" #sym "\", \"a\"	\n"	\
	    "	.balign	4					\n"	\
	    "__ksymtab_" #sym ":				\n"	\
	    "	.long	" #sym "- .				\n"	\
	    "	.long	__kstrtab_" #sym "- .			\n"	\
	    "	.long	__kstrtabns_" #sym "- .			\n"	\
	    "	.previous					\n")

struct kernel_symbol {
	int value_offset;
	int name_offset;
	int namespace_offset;
};

#ifdef __GENKSYMS__

#define ___EXPORT_SYMBOL(sym, sec, ns)	__GENKSYMS_EXPORT_SYMBOL(sym)

#else

#define ___EXPORT_SYMBOL(sym, sec, ns)						\
	extern typeof(sym) sym;							\
	extern const char __kstrtab_##sym[];					\
	extern const char __kstrtabns_##sym[];					\
	asm("	.section \"__ksymtab_strings\",\"aMS\",%progbits,1	\n"	\
	    "__kstrtab_" #sym ":					\n"	\
	    "	.asciz 	\"" #sym "\"					\n"	\
	    "__kstrtabns_" #sym ":					\n"	\
	    "	.asciz 	\"" ns "\"					\n"	\
	    "	.previous						\n");	\
	__KSYMTAB_ENTRY(sym, sec)

#endif


#define __EXPORT_SYMBOL(sym, sec, ns)


#ifdef DEFAULT_SYMBOL_NAMESPACE
#define _EXPORT_SYMBOL(sym, sec)	__EXPORT_SYMBOL(sym, sec, __stringify(DEFAULT_SYMBOL_NAMESPACE))
#else
#define _EXPORT_SYMBOL(sym, sec)	__EXPORT_SYMBOL(sym, sec, "")
#endif

#define EXPORT_SYMBOL(sym)		_EXPORT_SYMBOL(sym, "")
#define EXPORT_SYMBOL_GPL(sym)		_EXPORT_SYMBOL(sym, "_gpl")
#define EXPORT_SYMBOL_NS(sym, ns)	__EXPORT_SYMBOL(sym, "", __stringify(ns))
#define EXPORT_SYMBOL_NS_GPL(sym, ns)	__EXPORT_SYMBOL(sym, "_gpl", __stringify(ns))

#endif  

#endif  
