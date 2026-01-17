#ifndef __ASM_GENERIC_EXPORT_H
#define __ASM_GENERIC_EXPORT_H


#ifndef KSYM_FUNC
#define KSYM_FUNC(x) x
#endif
/* KSYM_ALIGN removed - unused */

.macro __put, val, name
	.long	\val - ., \name - ., 0
.endm


.macro ___EXPORT_SYMBOL name,val,sec
.endm

/* CONFIG_TRIM_UNUSED_KSYMS not set - using simple export */
#define __EXPORT_SYMBOL(sym, val, sec) ___EXPORT_SYMBOL sym, val, sec

#define EXPORT_SYMBOL(name)					\
	__EXPORT_SYMBOL(name, KSYM_FUNC(name),)
#define EXPORT_SYMBOL_GPL(name) 				\
	__EXPORT_SYMBOL(name, KSYM_FUNC(name), _gpl)
#define EXPORT_DATA_SYMBOL(name)				\
	__EXPORT_SYMBOL(name, name,)
#define EXPORT_DATA_SYMBOL_GPL(name)				\
	__EXPORT_SYMBOL(name, name,_gpl)

#endif
