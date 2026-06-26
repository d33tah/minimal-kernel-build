#ifndef __ASM_GENERIC_EXPORT_H
#define __ASM_GENERIC_EXPORT_H


#ifndef KSYM_FUNC
#define KSYM_FUNC(x) x
#endif

.macro ___EXPORT_SYMBOL name,val,sec
.endm

#define __EXPORT_SYMBOL(sym, val, sec) ___EXPORT_SYMBOL sym, val, sec

#define EXPORT_SYMBOL(name)					\
	__EXPORT_SYMBOL(name, KSYM_FUNC(name),)
#define EXPORT_SYMBOL_GPL(name) 				\
	__EXPORT_SYMBOL(name, KSYM_FUNC(name), _gpl)

#endif
