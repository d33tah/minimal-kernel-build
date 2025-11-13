// SPDX-License-Identifier: GPL-2.0-only
// Stubbed parser.c - minimal implementation for Hello World kernel
// Mount option parsing not needed for minimal boot

#include <linux/ctype.h>
#include <linux/types.h>
#include <linux/export.h>
#include <linux/parser.h>
#include <linux/slab.h>
#include <linux/string.h>

/**
 * match_token - Find a token (and optional args) in a string
 * @s: the string to examine for token/argument pairs
 * @table: match_table_t describing the set of allowed option tokens and the
 * arguments that may be associated with them. Must be terminated with a
 * &struct match_token whose pattern is set to the NULL pointer.
 * @args: array of %MAX_OPT_ARGS &substring_t elements. Used to return match
 * locations.
 *
 * Description: Detects which if any of a set of token strings has been passed
 * to it. Tokens can include up to MAX_OPT_ARGS instances of basic c-style
 * format identifiers which are taken to be arguments. Each such identifier is
 * taken to specify one substring_t, and the location  of the substring is
 * returned in the @args array.
 */
int match_token(char *s, const match_table_t table, substring_t args[])
{
	/* Stub: return error - MAX_OPT_ARGS typically used as Opt_err */
	return MAX_OPT_ARGS;
}

/**
 * match_int - scan a decimal representation of an integer from a substring_t
 * @s: substring_t to be scanned
 * @result: resulting integer on success
 *
 * Description: Attempts to parse the &substring_t @s as a decimal integer. On
 * success, sets @result to the integer represented by the string and returns 0.
 * Returns -ENOMEM, -EINVAL, or -ERANGE on failure.
 */
int match_int(substring_t *s, int *result)
{
	return -EINVAL;
}

/**
 * match_uint - scan a decimal representation of an integer from a substring_t
 * @s: substring_t to be scanned
 * @result: resulting integer on success
 *
 * Description: Attempts to parse the &substring_t @s as a decimal integer. On
 * success, sets @result to the integer represented by the string and returns 0.
 * Returns -ENOMEM, -EINVAL, or -ERANGE on failure.
 */
int match_uint(substring_t *s, unsigned int *result)
{
	return -EINVAL;
}

/**
 * match_u64 - scan a decimal representation of a u64 from
 *             a substring_t
 * @s: substring_t to be scanned
 * @result: resulting u64 on success
 *
 * Description: Attempts to parse the &substring_t @s as a long decimal
 * integer. On success, sets @result to the integer represented by the
 * string and returns 0.
 * Returns -ENOMEM, -EINVAL, or -ERANGE on failure.
 */
int match_u64(substring_t *s, u64 *result)
{
	return -EINVAL;
}

/**
 * match_octal - scan an octal representation of an integer from a substring_t
 * @s: substring_t to be scanned
 * @result: resulting integer on success
 *
 * Description: Attempts to parse the &substring_t @s as an octal integer. On
 * success, sets @result to the integer represented by the string and returns 0.
 * Returns -ENOMEM, -EINVAL, or -ERANGE on failure.
 */
int match_octal(substring_t *s, int *result)
{
	return -EINVAL;
}

/**
 * match_hex - scan a hex representation of an integer from a substring_t
 * @s: substring_t to be scanned
 * @result: resulting integer on success
 *
 * Description: Attempts to parse the &substring_t @s as a hexadecimal integer.
 * On success, sets @result to the integer represented by the string and
 * returns 0.
 * Returns -ENOMEM, -EINVAL, or -ERANGE on failure.
 */
int match_hex(substring_t *s, int *result)
{
	return -EINVAL;
}

/**
 * match_wildcard - parse if a string matches given wildcard pattern
 * @pattern: wildcard pattern
 * @str: the string to be parsed
 *
 * Description: Parse the string @str to check if matches wildcard
 * pattern @pattern. The pattern may contain two types of wildcards:
 *   '*' - matches zero or more characters
 *   '?' - matches one character
 * If it's matched, return true, else return false.
 */
bool match_wildcard(const char *pattern, const char *str)
{
	return false;
}

/**
 * match_strlcpy - Copy the characters from a substring_t to a sized buffer
 * @dest: where to copy to
 * @src: &substring_t to copy
 * @size: size of destination buffer
 *
 * Description: Copy the characters in &substring_t @src to the
 * c-style string @dest.  Copy no more than @size - 1 characters, plus
 * the terminating NUL.  Return length of @src.
 */
size_t match_strlcpy(char *dest, const substring_t *src, size_t size)
{
	if (size) {
		dest[0] = '\0';
	}
	return 0;
}

/**
 * match_strdup - allocate a new string with the contents of a substring_t
 * @s: &substring_t to copy
 *
 * Description: Allocates and returns a string filled with the contents of
 * the &substring_t @s. The caller is responsible for freeing the returned
 * string with kfree().
 */
char *match_strdup(const substring_t *s)
{
	return NULL;
}
