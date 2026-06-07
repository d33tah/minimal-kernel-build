
#include <linux/string.h>

#ifdef __HAVE_ARCH_STRCMP
int strcmp(const char *cs, const char *ct)
{
	while (*cs == *ct) {
		if (*cs == '\0')
			return 0;
		cs++;
		ct++;
	}
	return (unsigned char)*cs - (unsigned char)*ct;
}
#endif

#ifdef __HAVE_ARCH_STRNCMP
int strncmp(const char *cs, const char *ct, size_t count)
{
	while (count--) {
		if (*cs != *ct)
			return (unsigned char)*cs - (unsigned char)*ct;
		if (*cs == '\0')
			return 0;
		cs++;
		ct++;
	}
	return 0;
}
#endif

#ifdef __HAVE_ARCH_STRCHR
char *strchr(const char *s, int c)
{
	for (; *s != (char)c; s++)
		if (*s == '\0')
			return NULL;
	return (char *)s;
}
#endif

#ifdef __HAVE_ARCH_STRLEN
size_t strlen(const char *s)
{
	const char *sc = s;
	while (*sc != '\0')
		sc++;
	return sc - s;
}
#endif

#ifdef __HAVE_ARCH_STRNLEN
size_t strnlen(const char *s, size_t count)
{
	const char *sc = s;
	while (count-- && *sc != '\0')
		sc++;
	return sc - s;
}
#endif
