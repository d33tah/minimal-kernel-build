#ifndef TYPECHECK_H_INCLUDED
#define TYPECHECK_H_INCLUDED

#define typecheck(type,x) \
({	type __dummy; \
	typeof(x) __dummy2; \
	(void)(&__dummy == &__dummy2); \
	1; \
})

/* typecheck_fn, typecheck_pointer removed - never used */

#endif		 
