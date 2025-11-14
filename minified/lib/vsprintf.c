// SPDX-License-Identifier: GPL-2.0-only

#include <linux/stdarg.h>
#include <linux/build_bug.h>

#include <linux/errname.h>
#include <linux/module.h>	
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/math64.h>
#include <linux/uaccess.h>
#include <linux/ioport.h>
#include <linux/dcache.h>
#include <linux/cred.h>
#include <linux/rtc.h>
#include <linux/time.h>
#include <linux/uuid.h>
#include <linux/of.h>
// #include <net/addrconf.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/netdev_features.h>
#include <linux/random.h>
#include <linux/siphash.h>

/* Stubbed IPv6 definitions to avoid net/addrconf.h and net/ipv6.h dependency */
#define IPV6_FLOWINFO_MASK		cpu_to_be32(0x0FFFFFFF)

/* Stubbed IPv6 functions to avoid net/addrconf.h dependency */
static inline bool ipv6_addr_v4mapped(const struct in6_addr *a)
{
	return (
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
		*(unsigned long *)a |
#else
		(__force unsigned long)(a->s6_addr32[0] | a->s6_addr32[1]) |
#endif
		(__force unsigned long)(a->s6_addr32[2] ^
					cpu_to_be32(0x0000ffff))) == 0UL;
}

static inline bool ipv6_addr_is_isatap(const struct in6_addr *addr)
{
	return (addr->s6_addr32[2] | htonl(0x02000000)) == htonl(0x02005EFE);
}
#include <linux/compiler.h>
#include <linux/property.h>

#include "../mm/internal.h"	

#include <asm/page.h>		
#include <asm/byteorder.h>	
#include <asm/unaligned.h>

#include <linux/string_helpers.h>
#include "kstrtox.h"

bool no_hash_pointers __ro_after_init;

static noinline unsigned long long simple_strntoull(const char *startp, size_t max_chars, char **endp, unsigned int base)
{
	const char *cp;
	unsigned long long result = 0ULL;
	size_t prefix_chars;
	unsigned int rv;

	cp = _parse_integer_fixup_radix(startp, &base);
	prefix_chars = cp - startp;
	if (prefix_chars < max_chars) {
		rv = _parse_integer_limit(cp, base, &result, max_chars - prefix_chars);
		
		cp += (rv & ~KSTRTOX_OVERFLOW);
	} else {
		
		cp = startp + max_chars;
	}

	if (endp)
		*endp = (char *)cp;

	return result;
}

noinline
unsigned long long simple_strtoull(const char *cp, char **endp, unsigned int base)
{
	return simple_strntoull(cp, INT_MAX, endp, base);
}

unsigned long simple_strtoul(const char *cp, char **endp, unsigned int base)
{
	return simple_strtoull(cp, endp, base);
}

long simple_strtol(const char *cp, char **endp, unsigned int base)
{
	if (*cp == '-')
		return -simple_strtoul(cp + 1, endp, base);

	return simple_strtoul(cp, endp, base);
}

static long long simple_strntoll(const char *cp, size_t max_chars, char **endp,
				 unsigned int base)
{
	
	if (*cp == '-' && max_chars > 0)
		return -simple_strntoull(cp + 1, max_chars - 1, endp, base);

	return simple_strntoull(cp, max_chars, endp, base);
}

long long simple_strtoll(const char *cp, char **endp, unsigned int base)
{
	return simple_strntoll(cp, INT_MAX, endp, base);
}

static noinline_for_stack
int skip_atoi(const char **s)
{
	int i = 0;

	do {
		i = i*10 + *((*s)++) - '0';
	} while (isdigit(**s));

	return i;
}

static const u16 decpair[100] = {
#define _(x) (__force u16) cpu_to_le16(((x % 10) | ((x / 10) << 8)) + 0x3030)
	_( 0), _( 1), _( 2), _( 3), _( 4), _( 5), _( 6), _( 7), _( 8), _( 9),
	_(10), _(11), _(12), _(13), _(14), _(15), _(16), _(17), _(18), _(19),
	_(20), _(21), _(22), _(23), _(24), _(25), _(26), _(27), _(28), _(29),
	_(30), _(31), _(32), _(33), _(34), _(35), _(36), _(37), _(38), _(39),
	_(40), _(41), _(42), _(43), _(44), _(45), _(46), _(47), _(48), _(49),
	_(50), _(51), _(52), _(53), _(54), _(55), _(56), _(57), _(58), _(59),
	_(60), _(61), _(62), _(63), _(64), _(65), _(66), _(67), _(68), _(69),
	_(70), _(71), _(72), _(73), _(74), _(75), _(76), _(77), _(78), _(79),
	_(80), _(81), _(82), _(83), _(84), _(85), _(86), _(87), _(88), _(89),
	_(90), _(91), _(92), _(93), _(94), _(95), _(96), _(97), _(98), _(99),
#undef _
};

static noinline_for_stack
char *put_dec_trunc8(char *buf, unsigned r)
{
	unsigned q;

	
	if (r < 100)
		goto out_r;

	
	q = (r * (u64)0x28f5c29) >> 32;
	*((u16 *)buf) = decpair[r - 100*q];
	buf += 2;

	
	if (q < 100)
		goto out_q;

	
	r = (q * (u64)0x28f5c29) >> 32;
	*((u16 *)buf) = decpair[q - 100*r];
	buf += 2;

	
	if (r < 100)
		goto out_r;

	
	q = (r * 0x147b) >> 19;
	*((u16 *)buf) = decpair[r - 100*q];
	buf += 2;
out_q:
	
	r = q;
out_r:
	
	*((u16 *)buf) = decpair[r];
	buf += r < 10 ? 1 : 2;
	return buf;
}

#if BITS_PER_LONG == 64 && BITS_PER_LONG_LONG == 64
static noinline_for_stack
char *put_dec_full8(char *buf, unsigned r)
{
	unsigned q;

	
	q = (r * (u64)0x28f5c29) >> 32;
	*((u16 *)buf) = decpair[r - 100*q];
	buf += 2;

	
	r = (q * (u64)0x28f5c29) >> 32;
	*((u16 *)buf) = decpair[q - 100*r];
	buf += 2;

	
	q = (r * 0x147b) >> 19;
	*((u16 *)buf) = decpair[r - 100*q];
	buf += 2;

	
	*((u16 *)buf) = decpair[q];
	buf += 2;
	return buf;
}

static noinline_for_stack
char *put_dec(char *buf, unsigned long long n)
{
	if (n >= 100*1000*1000)
		buf = put_dec_full8(buf, do_div(n, 100*1000*1000));
	
	if (n >= 100*1000*1000)
		buf = put_dec_full8(buf, do_div(n, 100*1000*1000));
	
	return put_dec_trunc8(buf, n);
}

#elif BITS_PER_LONG == 32 && BITS_PER_LONG_LONG == 64

static void
put_dec_full4(char *buf, unsigned r)
{
	unsigned q;

	
	q = (r * 0x147b) >> 19;
	*((u16 *)buf) = decpair[r - 100*q];
	buf += 2;
	
	*((u16 *)buf) = decpair[q];
}

static noinline_for_stack
unsigned put_dec_helper4(char *buf, unsigned x)
{
        uint32_t q = (x * (uint64_t)0x346DC5D7) >> 43;

        put_dec_full4(buf, x - q * 10000);
        return q;
}

static
char *put_dec(char *buf, unsigned long long n)
{
	uint32_t d3, d2, d1, q, h;

	if (n < 100*1000*1000)
		return put_dec_trunc8(buf, n);

	d1  = ((uint32_t)n >> 16); 
	h   = (n >> 32);
	d2  = (h      ) & 0xffff;
	d3  = (h >> 16); 

	
	q   = 656 * d3 + 7296 * d2 + 5536 * d1 + ((uint32_t)n & 0xffff);
	q = put_dec_helper4(buf, q);

	q += 7671 * d3 + 9496 * d2 + 6 * d1;
	q = put_dec_helper4(buf+4, q);

	q += 4749 * d3 + 42 * d2;
	q = put_dec_helper4(buf+8, q);

	q += 281 * d3;
	buf += 12;
	if (q)
		buf = put_dec_trunc8(buf, q);
	else while (buf[-1] == '0')
		--buf;

	return buf;
}

#endif

int num_to_str(char *buf, int size, unsigned long long num, unsigned int width)
{
	
	char tmp[sizeof(num) * 3] __aligned(2);
	int idx, len;

	
	if (num <= 9) {
		tmp[0] = '0' + num;
		len = 1;
	} else {
		len = put_dec(tmp, num) - tmp;
	}

	if (len > size || width > size)
		return 0;

	if (width > len) {
		width = width - len;
		for (idx = 0; idx < width; idx++)
			buf[idx] = ' ';
	} else {
		width = 0;
	}

	for (idx = 0; idx < len; ++idx)
		buf[idx + width] = tmp[len - idx - 1];

	return len + width;
}

#define SIGN	1		
#define LEFT	2		
#define PLUS	4		
#define SPACE	8		
#define ZEROPAD	16		
#define SMALL	32		
#define SPECIAL	64		

static_assert(SIGN == 1);
static_assert(ZEROPAD == ('0' - ' '));
static_assert(SMALL == ('a' ^ 'A'));

enum format_type {
	FORMAT_TYPE_NONE, 
	FORMAT_TYPE_WIDTH,
	FORMAT_TYPE_PRECISION,
	FORMAT_TYPE_CHAR,
	FORMAT_TYPE_STR,
	FORMAT_TYPE_PTR,
	FORMAT_TYPE_PERCENT_CHAR,
	FORMAT_TYPE_INVALID,
	FORMAT_TYPE_LONG_LONG,
	FORMAT_TYPE_ULONG,
	FORMAT_TYPE_LONG,
	FORMAT_TYPE_UBYTE,
	FORMAT_TYPE_BYTE,
	FORMAT_TYPE_USHORT,
	FORMAT_TYPE_SHORT,
	FORMAT_TYPE_UINT,
	FORMAT_TYPE_INT,
	FORMAT_TYPE_SIZE_T,
	FORMAT_TYPE_PTRDIFF
};

struct printf_spec {
	unsigned int	type:8;		
	signed int	field_width:24;	
	unsigned int	flags:8;	
	unsigned int	base:8;		
	signed int	precision:16;	
} __packed;
static_assert(sizeof(struct printf_spec) == 8);

#define FIELD_WIDTH_MAX ((1 << 23) - 1)
#define PRECISION_MAX ((1 << 15) - 1)

static noinline_for_stack
char *number(char *buf, char *end, unsigned long long num,
	     struct printf_spec spec)
{
	
	char tmp[3 * sizeof(num)] __aligned(2);
	char sign;
	char locase;
	int need_pfx = ((spec.flags & SPECIAL) && spec.base != 10);
	int i;
	bool is_zero = num == 0LL;
	int field_width = spec.field_width;
	int precision = spec.precision;

	
	locase = (spec.flags & SMALL);
	if (spec.flags & LEFT)
		spec.flags &= ~ZEROPAD;
	sign = 0;
	if (spec.flags & SIGN) {
		if ((signed long long)num < 0) {
			sign = '-';
			num = -(signed long long)num;
			field_width--;
		} else if (spec.flags & PLUS) {
			sign = '+';
			field_width--;
		} else if (spec.flags & SPACE) {
			sign = ' ';
			field_width--;
		}
	}
	if (need_pfx) {
		if (spec.base == 16)
			field_width -= 2;
		else if (!is_zero)
			field_width--;
	}

	
	i = 0;
	if (num < spec.base)
		tmp[i++] = hex_asc_upper[num] | locase;
	else if (spec.base != 10) { 
		int mask = spec.base - 1;
		int shift = 3;

		if (spec.base == 16)
			shift = 4;
		do {
			tmp[i++] = (hex_asc_upper[((unsigned char)num) & mask] | locase);
			num >>= shift;
		} while (num);
	} else { 
		i = put_dec(tmp, num) - tmp;
	}

	
	if (i > precision)
		precision = i;
	
	field_width -= precision;
	if (!(spec.flags & (ZEROPAD | LEFT))) {
		while (--field_width >= 0) {
			if (buf < end)
				*buf = ' ';
			++buf;
		}
	}
	
	if (sign) {
		if (buf < end)
			*buf = sign;
		++buf;
	}
	
	if (need_pfx) {
		if (spec.base == 16 || !is_zero) {
			if (buf < end)
				*buf = '0';
			++buf;
		}
		if (spec.base == 16) {
			if (buf < end)
				*buf = ('X' | locase);
			++buf;
		}
	}
	
	if (!(spec.flags & LEFT)) {
		char c = ' ' + (spec.flags & ZEROPAD);

		while (--field_width >= 0) {
			if (buf < end)
				*buf = c;
			++buf;
		}
	}
	
	while (i <= --precision) {
		if (buf < end)
			*buf = '0';
		++buf;
	}
	
	while (--i >= 0) {
		if (buf < end)
			*buf = tmp[i];
		++buf;
	}
	
	while (--field_width >= 0) {
		if (buf < end)
			*buf = ' ';
		++buf;
	}

	return buf;
}

static noinline_for_stack
char *special_hex_number(char *buf, char *end, unsigned long long num, int size)
{
	struct printf_spec spec;

	spec.type = FORMAT_TYPE_PTR;
	spec.field_width = 2 + 2 * size;	
	spec.flags = SPECIAL | SMALL | ZEROPAD;
	spec.base = 16;
	spec.precision = -1;

	return number(buf, end, num, spec);
}

static void move_right(char *buf, char *end, unsigned len, unsigned spaces)
{
	size_t size;
	if (buf >= end)	
		return;
	size = end - buf;
	if (size <= spaces) {
		memset(buf, ' ', size);
		return;
	}
	if (len) {
		if (len > size - spaces)
			len = size - spaces;
		memmove(buf + spaces, buf, len);
	}
	memset(buf, ' ', spaces);
}

static noinline_for_stack
char *widen_string(char *buf, int n, char *end, struct printf_spec spec)
{
	unsigned spaces;

	if (likely(n >= spec.field_width))
		return buf;
	
	spaces = spec.field_width - n;
	if (!(spec.flags & LEFT)) {
		move_right(buf - n, end, n, spaces);
		return buf + spaces;
	}
	while (spaces--) {
		if (buf < end)
			*buf = ' ';
		++buf;
	}
	return buf;
}

static char *string_nocheck(char *buf, char *end, const char *s,
			    struct printf_spec spec)
{
	int len = 0;
	int lim = spec.precision;

	while (lim--) {
		char c = *s++;
		if (!c)
			break;
		if (buf < end)
			*buf = c;
		++buf;
		++len;
	}
	return widen_string(buf, len, end, spec);
}

static char *err_ptr(char *buf, char *end, void *ptr,
		     struct printf_spec spec)
{
	int err = PTR_ERR(ptr);
	const char *sym = errname(err);

	if (sym)
		return string_nocheck(buf, end, sym, spec);

	
	spec.flags |= SIGN;
	spec.base = 10;
	return number(buf, end, err, spec);
}

static char *error_string(char *buf, char *end, const char *s,
			  struct printf_spec spec)
{
	
	if (spec.precision == -1)
		spec.precision = 2 * sizeof(void *);

	return string_nocheck(buf, end, s, spec);
}

static const char *check_pointer_msg(const void *ptr)
{
	if (!ptr)
		return "(null)";

	if ((unsigned long)ptr < PAGE_SIZE || IS_ERR_VALUE(ptr))
		return "(efault)";

	return NULL;
}

static int check_pointer(char **buf, char *end, const void *ptr,
			 struct printf_spec spec)
{
	const char *err_msg;

	err_msg = check_pointer_msg(ptr);
	if (err_msg) {
		*buf = error_string(*buf, end, err_msg, spec);
		return -EFAULT;
	}

	return 0;
}

static noinline_for_stack
char *string(char *buf, char *end, const char *s,
	     struct printf_spec spec)
{
	if (check_pointer(&buf, end, s, spec))
		return buf;

	return string_nocheck(buf, end, s, spec);
}

static char *pointer_string(char *buf, char *end,
			    const void *ptr,
			    struct printf_spec spec)
{
	spec.base = 16;
	spec.flags |= SMALL;
	if (spec.field_width == -1) {
		spec.field_width = 2 * sizeof(ptr);
		spec.flags |= ZEROPAD;
	}

	return number(buf, end, (unsigned long int)ptr, spec);
}

static int debug_boot_weak_hash __ro_after_init;

static int __init debug_boot_weak_hash_enable(char *str)
{
	debug_boot_weak_hash = 1;
	pr_info("debug_boot_weak_hash enabled\n");
	return 0;
}
early_param("debug_boot_weak_hash", debug_boot_weak_hash_enable);

static DEFINE_STATIC_KEY_FALSE(filled_random_ptr_key);

static void enable_ptr_key_workfn(struct work_struct *work)
{
	static_branch_enable(&filled_random_ptr_key);
}

static inline int __ptr_to_hashval(const void *ptr, unsigned long *hashval_out)
{
	static siphash_key_t ptr_key __read_mostly;
	unsigned long hashval;

	if (!static_branch_likely(&filled_random_ptr_key)) {
		static bool filled = false;
		static DEFINE_SPINLOCK(filling);
		static DECLARE_WORK(enable_ptr_key_work, enable_ptr_key_workfn);
		unsigned long flags;

		if (!system_unbound_wq || !rng_is_initialized() ||
		    !spin_trylock_irqsave(&filling, flags))
			return -EAGAIN;

		if (!filled) {
			get_random_bytes(&ptr_key, sizeof(ptr_key));
			queue_work(system_unbound_wq, &enable_ptr_key_work);
			filled = true;
		}
		spin_unlock_irqrestore(&filling, flags);
	}

	hashval = (unsigned long)siphash_1u32((u32)ptr, &ptr_key);
	*hashval_out = hashval;
	return 0;
}

int ptr_to_hashval(const void *ptr, unsigned long *hashval_out)
{
	return __ptr_to_hashval(ptr, hashval_out);
}

static char *ptr_to_id(char *buf, char *end, const void *ptr,
		       struct printf_spec spec)
{
	const char *str = sizeof(ptr) == 8 ? "(____ptrval____)" : "(ptrval)";
	unsigned long hashval;
	int ret;

	
	if (IS_ERR_OR_NULL(ptr))
		return pointer_string(buf, end, ptr, spec);

	
	if (unlikely(debug_boot_weak_hash)) {
		hashval = hash_long((unsigned long)ptr, 32);
		return pointer_string(buf, end, (const void *)hashval, spec);
	}

	ret = __ptr_to_hashval(ptr, &hashval);
	if (ret) {
		spec.field_width = 2 * sizeof(ptr);
		
		return error_string(buf, end, str, spec);
	}

	return pointer_string(buf, end, (const void *)hashval, spec);
}

static char *default_pointer(char *buf, char *end, const void *ptr,
			     struct printf_spec spec)
{
	
	if (unlikely(no_hash_pointers))
		return pointer_string(buf, end, ptr, spec);

	return ptr_to_id(buf, end, ptr, spec);
}

int kptr_restrict __read_mostly;

static noinline_for_stack
char *restricted_pointer(char *buf, char *end, const void *ptr,
			 struct printf_spec spec)
{
	switch (kptr_restrict) {
	case 0:
		
		return default_pointer(buf, end, ptr, spec);
	case 1: {
		const struct cred *cred;

		
		if (in_irq() || in_serving_softirq() || in_nmi()) {
			if (spec.field_width == -1)
				spec.field_width = 2 * sizeof(ptr);
			return error_string(buf, end, "pK-error", spec);
		}

		
		cred = current_cred();
		if (!has_capability_noaudit(current, CAP_SYSLOG) ||
		    !uid_eq(cred->euid, cred->uid) ||
		    !gid_eq(cred->egid, cred->gid))
			ptr = NULL;
		break;
	}
	case 2:
	default:
		
		ptr = NULL;
		break;
	}

	return pointer_string(buf, end, ptr, spec);
}

static noinline_for_stack
char *dentry_name(char *buf, char *end, const struct dentry *d, struct printf_spec spec,
		  const char *fmt)
{
	/* Stubbed: dentry formatting not needed for minimal kernel */
	return error_string(buf, end, "(dentry)", spec);
}

static noinline_for_stack
char *file_dentry_name(char *buf, char *end, const struct file *f,
			struct printf_spec spec, const char *fmt)
{
	/* Stubbed: file dentry formatting not needed for minimal kernel */
	return error_string(buf, end, "(file)", spec);
}

static noinline_for_stack
char *symbol_string(char *buf, char *end, void *ptr,
		    struct printf_spec spec, const char *fmt)
{
	unsigned long value;

	if (fmt[1] == 'R')
		ptr = __builtin_extract_return_addr(ptr);
	value = (unsigned long)ptr;

	return special_hex_number(buf, end, value, sizeof(void *));
}

static const struct printf_spec default_str_spec = {
	.field_width = -1,
	.precision = -1,
};

static const struct printf_spec default_flag_spec = {
	.base = 16,
	.precision = -1,
	.flags = SPECIAL | SMALL,
};

static const struct printf_spec default_dec_spec = {
	.base = 10,
	.precision = -1,
};

static const struct printf_spec default_dec02_spec = {
	.base = 10,
	.field_width = 2,
	.precision = -1,
	.flags = ZEROPAD,
};

static const struct printf_spec default_dec04_spec = {
	.base = 10,
	.field_width = 4,
	.precision = -1,
	.flags = ZEROPAD,
};

static noinline_for_stack
char *resource_string(char *buf, char *end, struct resource *res,
		      struct printf_spec spec, const char *fmt)
{
	/* Stubbed: resource formatting not needed for minimal kernel */
	return error_string(buf, end, "(rsrc)", spec);
}

static noinline_for_stack
char *hex_string(char *buf, char *end, u8 *addr, struct printf_spec spec,
		 const char *fmt)
{
	/* Stubbed: hex dump formatting not needed for minimal kernel */
	return error_string(buf, end, "(hex)", spec);
}

static noinline_for_stack
char *bitmap_string(char *buf, char *end, unsigned long *bitmap,
		    struct printf_spec spec, const char *fmt)
{
	/* Stubbed: bitmap formatting not needed for minimal kernel */
	return error_string(buf, end, "(bitmap)", spec);
}

static noinline_for_stack
char *bitmap_list_string(char *buf, char *end, unsigned long *bitmap,
			 struct printf_spec spec, const char *fmt)
{
	/* Stubbed: bitmap list formatting not needed for minimal kernel */
	return error_string(buf, end, "(blist)", spec);
}

static noinline_for_stack
char *mac_address_string(char *buf, char *end, u8 *addr,
			 struct printf_spec spec, const char *fmt)
{
	/* Stubbed: MAC address formatting not needed for minimal kernel */
	return error_string(buf, end, "(mac)", spec);
}

static noinline_for_stack
char *ip4_string(char *p, const u8 *addr, const char *fmt)
{
	int i;
	bool leading_zeros = (fmt[0] == 'i');
	int index;
	int step;

	switch (fmt[2]) {
	case 'h':
#ifdef __BIG_ENDIAN
		index = 0;
		step = 1;
#else
		index = 3;
		step = -1;
#endif
		break;
	case 'l':
		index = 3;
		step = -1;
		break;
	case 'n':
	case 'b':
	default:
		index = 0;
		step = 1;
		break;
	}
	for (i = 0; i < 4; i++) {
		char temp[4] __aligned(2);	
		int digits = put_dec_trunc8(temp, addr[index]) - temp;
		if (leading_zeros) {
			if (digits < 3)
				*p++ = '0';
			if (digits < 2)
				*p++ = '0';
		}
		
		while (digits--)
			*p++ = temp[digits];
		if (i < 3)
			*p++ = '.';
		index += step;
	}
	*p = '\0';

	return p;
}

static noinline_for_stack
char *ip6_compressed_string(char *p, const char *addr)
{
	int i, j, range;
	unsigned char zerolength[8];
	int longest = 1;
	int colonpos = -1;
	u16 word;
	u8 hi, lo;
	bool needcolon = false;
	bool useIPv4;
	struct in6_addr in6;

	memcpy(&in6, addr, sizeof(struct in6_addr));

	useIPv4 = ipv6_addr_v4mapped(&in6) || ipv6_addr_is_isatap(&in6);

	memset(zerolength, 0, sizeof(zerolength));

	if (useIPv4)
		range = 6;
	else
		range = 8;

	
	for (i = 0; i < range; i++) {
		for (j = i; j < range; j++) {
			if (in6.s6_addr16[j] != 0)
				break;
			zerolength[i]++;
		}
	}
	for (i = 0; i < range; i++) {
		if (zerolength[i] > longest) {
			longest = zerolength[i];
			colonpos = i;
		}
	}
	if (longest == 1)		
		colonpos = -1;

	
	for (i = 0; i < range; i++) {
		if (i == colonpos) {
			if (needcolon || i == 0)
				*p++ = ':';
			*p++ = ':';
			needcolon = false;
			i += longest - 1;
			continue;
		}
		if (needcolon) {
			*p++ = ':';
			needcolon = false;
		}
		
		word = ntohs(in6.s6_addr16[i]);
		hi = word >> 8;
		lo = word & 0xff;
		if (hi) {
			if (hi > 0x0f)
				p = hex_byte_pack(p, hi);
			else
				*p++ = hex_asc_lo(hi);
			p = hex_byte_pack(p, lo);
		}
		else if (lo > 0x0f)
			p = hex_byte_pack(p, lo);
		else
			*p++ = hex_asc_lo(lo);
		needcolon = true;
	}

	if (useIPv4) {
		if (needcolon)
			*p++ = ':';
		p = ip4_string(p, &in6.s6_addr[12], "I4");
	}
	*p = '\0';

	return p;
}

static noinline_for_stack
char *ip6_string(char *p, const char *addr, const char *fmt)
{
	int i;

	for (i = 0; i < 8; i++) {
		p = hex_byte_pack(p, *addr++);
		p = hex_byte_pack(p, *addr++);
		if (fmt[0] == 'I' && i != 7)
			*p++ = ':';
	}
	*p = '\0';

	return p;
}

static noinline_for_stack
char *ip6_addr_string(char *buf, char *end, const u8 *addr,
		      struct printf_spec spec, const char *fmt)
{
	char ip6_addr[sizeof("xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:255.255.255.255")];

	if (fmt[0] == 'I' && fmt[2] == 'c')
		ip6_compressed_string(ip6_addr, addr);
	else
		ip6_string(ip6_addr, addr, fmt);

	return string_nocheck(buf, end, ip6_addr, spec);
}

static noinline_for_stack
char *ip4_addr_string(char *buf, char *end, const u8 *addr,
		      struct printf_spec spec, const char *fmt)
{
	char ip4_addr[sizeof("255.255.255.255")];

	ip4_string(ip4_addr, addr, fmt);

	return string_nocheck(buf, end, ip4_addr, spec);
}

static noinline_for_stack
char *ip6_addr_string_sa(char *buf, char *end, const struct sockaddr_in6 *sa,
			 struct printf_spec spec, const char *fmt)
{
	bool have_p = false, have_s = false, have_f = false, have_c = false;
	char ip6_addr[sizeof("[xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:255.255.255.255]") +
		      sizeof(":12345") + sizeof("/123456789") +
		      sizeof("%1234567890")];
	char *p = ip6_addr, *pend = ip6_addr + sizeof(ip6_addr);
	const u8 *addr = (const u8 *) &sa->sin6_addr;
	char fmt6[2] = { fmt[0], '6' };
	u8 off = 0;

	fmt++;
	while (isalpha(*++fmt)) {
		switch (*fmt) {
		case 'p':
			have_p = true;
			break;
		case 'f':
			have_f = true;
			break;
		case 's':
			have_s = true;
			break;
		case 'c':
			have_c = true;
			break;
		}
	}

	if (have_p || have_s || have_f) {
		*p = '[';
		off = 1;
	}

	if (fmt6[0] == 'I' && have_c)
		p = ip6_compressed_string(ip6_addr + off, addr);
	else
		p = ip6_string(ip6_addr + off, addr, fmt6);

	if (have_p || have_s || have_f)
		*p++ = ']';

	if (have_p) {
		*p++ = ':';
		p = number(p, pend, ntohs(sa->sin6_port), spec);
	}
	if (have_f) {
		*p++ = '/';
		p = number(p, pend, ntohl(sa->sin6_flowinfo &
					  IPV6_FLOWINFO_MASK), spec);
	}
	if (have_s) {
		*p++ = '%';
		p = number(p, pend, sa->sin6_scope_id, spec);
	}
	*p = '\0';

	return string_nocheck(buf, end, ip6_addr, spec);
}

static noinline_for_stack
char *ip4_addr_string_sa(char *buf, char *end, const struct sockaddr_in *sa,
			 struct printf_spec spec, const char *fmt)
{
	bool have_p = false;
	char *p, ip4_addr[sizeof("255.255.255.255") + sizeof(":12345")];
	char *pend = ip4_addr + sizeof(ip4_addr);
	const u8 *addr = (const u8 *) &sa->sin_addr.s_addr;
	char fmt4[3] = { fmt[0], '4', 0 };

	fmt++;
	while (isalpha(*++fmt)) {
		switch (*fmt) {
		case 'p':
			have_p = true;
			break;
		case 'h':
		case 'l':
		case 'n':
		case 'b':
			fmt4[2] = *fmt;
			break;
		}
	}

	p = ip4_string(ip4_addr, addr, fmt4);
	if (have_p) {
		*p++ = ':';
		p = number(p, pend, ntohs(sa->sin_port), spec);
	}
	*p = '\0';

	return string_nocheck(buf, end, ip4_addr, spec);
}

static noinline_for_stack
char *ip_addr_string(char *buf, char *end, const void *ptr,
		     struct printf_spec spec, const char *fmt)
{
	/* Stubbed: IP address formatting not needed for minimal kernel */
	return error_string(buf, end, "(ip)", spec);
}

static noinline_for_stack
char *escaped_string(char *buf, char *end, u8 *addr, struct printf_spec spec,
		     const char *fmt)
{
	/* Stubbed: string escaping not needed for minimal kernel */
	return error_string(buf, end, "(esc)", spec);
}

static char *va_format(char *buf, char *end, struct va_format *va_fmt,
		       struct printf_spec spec, const char *fmt)
{
	va_list va;

	if (check_pointer(&buf, end, va_fmt, spec))
		return buf;

	va_copy(va, *va_fmt->va);
	buf += vsnprintf(buf, end > buf ? end - buf : 0, va_fmt->fmt, va);
	va_end(va);

	return buf;
}

static noinline_for_stack
char *uuid_string(char *buf, char *end, const u8 *addr,
		  struct printf_spec spec, const char *fmt)
{
	/* Stubbed: UUID formatting not needed for minimal kernel */
	return error_string(buf, end, "(uuid)", spec);
}

static noinline_for_stack
char *netdev_bits(char *buf, char *end, const void *addr,
		  struct printf_spec spec,  const char *fmt)
{
	/* Stubbed: netdev formatting not needed for minimal kernel */
	return error_string(buf, end, "(netdev)", spec);
}

static noinline_for_stack
char *fourcc_string(char *buf, char *end, const u32 *fourcc,
		    struct printf_spec spec, const char *fmt)
{
	/* Stubbed: fourcc formatting not needed for minimal kernel */
	return error_string(buf, end, "(fourcc)", spec);
}

static noinline_for_stack
char *address_val(char *buf, char *end, const void *addr,
		  struct printf_spec spec, const char *fmt)
{
	unsigned long long num;
	int size;

	if (check_pointer(&buf, end, addr, spec))
		return buf;

	switch (fmt[1]) {
	case 'd':
		num = *(const dma_addr_t *)addr;
		size = sizeof(dma_addr_t);
		break;
	case 'p':
	default:
		num = *(const phys_addr_t *)addr;
		size = sizeof(phys_addr_t);
		break;
	}

	return special_hex_number(buf, end, num, size);
}

static noinline_for_stack
char *time64_str(char *buf, char *end, const time64_t time,
		 struct printf_spec spec, const char *fmt)
{
	/* Stubbed: time/date formatting not needed for minimal kernel */
	return error_string(buf, end, "(time)", spec);
}

static noinline_for_stack
char *time_and_date(char *buf, char *end, void *ptr, struct printf_spec spec,
		    const char *fmt)
{
	/* Stubbed: time/date formatting not needed for minimal kernel */
	return error_string(buf, end, "(time)", spec);
}

static noinline_for_stack
char *clock(char *buf, char *end, struct clk *clk, struct printf_spec spec,
	    const char *fmt)
{
	/* Stubbed: clock formatting not needed for minimal kernel */
	return error_string(buf, end, "(clock)", spec);
}

static noinline_for_stack
char *flags_string(char *buf, char *end, void *flags_ptr,
		   struct printf_spec spec, const char *fmt)
{
	/* Stubbed: flags formatting not needed for minimal kernel */
	return error_string(buf, end, "(flags)", spec);
}

static noinline_for_stack
char *fwnode_full_name_string(struct fwnode_handle *fwnode, char *buf,
			      char *end)
{
	/* Stubbed: firmware node formatting not needed for minimal kernel */
	return string(buf, end, "(fwnode)", default_str_spec);
}

static noinline_for_stack
char *device_node_string(char *buf, char *end, struct device_node *dn,
			 struct printf_spec spec, const char *fmt)
{
	/* Stubbed: device node formatting not needed for minimal kernel */
	return error_string(buf, end, "(devnode)", spec);
}

static noinline_for_stack
char *fwnode_string(char *buf, char *end, struct fwnode_handle *fwnode,
		    struct printf_spec spec, const char *fmt)
{
	/* Stubbed: fwnode formatting not needed for minimal kernel */
	return error_string(buf, end, "(fwnode)", spec);
}

int __init no_hash_pointers_enable(char *str)
{
	if (no_hash_pointers)
		return 0;

	no_hash_pointers = true;

	pr_warn("**********************************************************\n");
	pr_warn("**   NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE   **\n");
	pr_warn("**                                                      **\n");
	pr_warn("** This system shows unhashed kernel memory addresses   **\n");
	pr_warn("** via the console, logs, and other interfaces. This    **\n");
	pr_warn("** might reduce the security of your system.            **\n");
	pr_warn("**                                                      **\n");
	pr_warn("** If you see this message and you are not debugging    **\n");
	pr_warn("** the kernel, report this immediately to your system   **\n");
	pr_warn("** administrator!                                       **\n");
	pr_warn("**                                                      **\n");
	pr_warn("**   NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE   **\n");
	pr_warn("**********************************************************\n");

	return 0;
}
early_param("no_hash_pointers", no_hash_pointers_enable);

static noinline_for_stack
char *pointer(const char *fmt, char *buf, char *end, void *ptr,
	      struct printf_spec spec)
{
	switch (*fmt) {
	case 'S':
	case 's':
		ptr = dereference_symbol_descriptor(ptr);
		fallthrough;
	case 'B':
		return symbol_string(buf, end, ptr, spec, fmt);
	case 'R':
	case 'r':
		return resource_string(buf, end, ptr, spec, fmt);
	case 'h':
		return hex_string(buf, end, ptr, spec, fmt);
	case 'b':
		switch (fmt[1]) {
		case 'l':
			return bitmap_list_string(buf, end, ptr, spec, fmt);
		default:
			return bitmap_string(buf, end, ptr, spec, fmt);
		}
	case 'M':			
	case 'm':			
					
					
		return mac_address_string(buf, end, ptr, spec, fmt);
	case 'I':			
	case 'i':			
		return ip_addr_string(buf, end, ptr, spec, fmt);
	case 'E':
		return escaped_string(buf, end, ptr, spec, fmt);
	case 'U':
		return uuid_string(buf, end, ptr, spec, fmt);
	case 'V':
		return va_format(buf, end, ptr, spec, fmt);
	case 'K':
		return restricted_pointer(buf, end, ptr, spec);
	case 'N':
		return netdev_bits(buf, end, ptr, spec, fmt);
	case '4':
		return fourcc_string(buf, end, ptr, spec, fmt);
	case 'a':
		return address_val(buf, end, ptr, spec, fmt);
	case 'd':
		return dentry_name(buf, end, ptr, spec, fmt);
	case 't':
		return time_and_date(buf, end, ptr, spec, fmt);
	case 'C':
		return clock(buf, end, ptr, spec, fmt);
	case 'D':
		return file_dentry_name(buf, end, ptr, spec, fmt);

	case 'G':
		return flags_string(buf, end, ptr, spec, fmt);
	case 'O':
		return device_node_string(buf, end, ptr, spec, fmt + 1);
	case 'f':
		return fwnode_string(buf, end, ptr, spec, fmt + 1);
	case 'x':
		return pointer_string(buf, end, ptr, spec);
	case 'e':
		
		if (!IS_ERR(ptr))
			return default_pointer(buf, end, ptr, spec);
		return err_ptr(buf, end, ptr, spec);
	case 'u':
	case 'k':
		switch (fmt[1]) {
		case 's':
			return string(buf, end, ptr, spec);
		default:
			return error_string(buf, end, "(einval)", spec);
		}
	default:
		return default_pointer(buf, end, ptr, spec);
	}
}

static noinline_for_stack
int format_decode(const char *fmt, struct printf_spec *spec)
{
	const char *start = fmt;
	char qualifier;

	
	if (spec->type == FORMAT_TYPE_WIDTH) {
		if (spec->field_width < 0) {
			spec->field_width = -spec->field_width;
			spec->flags |= LEFT;
		}
		spec->type = FORMAT_TYPE_NONE;
		goto precision;
	}

	
	if (spec->type == FORMAT_TYPE_PRECISION) {
		if (spec->precision < 0)
			spec->precision = 0;

		spec->type = FORMAT_TYPE_NONE;
		goto qualifier;
	}

	
	spec->type = FORMAT_TYPE_NONE;

	for (; *fmt ; ++fmt) {
		if (*fmt == '%')
			break;
	}

	
	if (fmt != start || !*fmt)
		return fmt - start;

	
	spec->flags = 0;

	while (1) { 
		bool found = true;

		++fmt;

		switch (*fmt) {
		case '-': spec->flags |= LEFT;    break;
		case '+': spec->flags |= PLUS;    break;
		case ' ': spec->flags |= SPACE;   break;
		case '#': spec->flags |= SPECIAL; break;
		case '0': spec->flags |= ZEROPAD; break;
		default:  found = false;
		}

		if (!found)
			break;
	}

	
	spec->field_width = -1;

	if (isdigit(*fmt))
		spec->field_width = skip_atoi(&fmt);
	else if (*fmt == '*') {
		
		spec->type = FORMAT_TYPE_WIDTH;
		return ++fmt - start;
	}

precision:
	
	spec->precision = -1;
	if (*fmt == '.') {
		++fmt;
		if (isdigit(*fmt)) {
			spec->precision = skip_atoi(&fmt);
			if (spec->precision < 0)
				spec->precision = 0;
		} else if (*fmt == '*') {
			
			spec->type = FORMAT_TYPE_PRECISION;
			return ++fmt - start;
		}
	}

qualifier:
	
	qualifier = 0;
	if (*fmt == 'h' || _tolower(*fmt) == 'l' ||
	    *fmt == 'z' || *fmt == 't') {
		qualifier = *fmt++;
		if (unlikely(qualifier == *fmt)) {
			if (qualifier == 'l') {
				qualifier = 'L';
				++fmt;
			} else if (qualifier == 'h') {
				qualifier = 'H';
				++fmt;
			}
		}
	}

	
	spec->base = 10;
	switch (*fmt) {
	case 'c':
		spec->type = FORMAT_TYPE_CHAR;
		return ++fmt - start;

	case 's':
		spec->type = FORMAT_TYPE_STR;
		return ++fmt - start;

	case 'p':
		spec->type = FORMAT_TYPE_PTR;
		return ++fmt - start;

	case '%':
		spec->type = FORMAT_TYPE_PERCENT_CHAR;
		return ++fmt - start;

	
	case 'o':
		spec->base = 8;
		break;

	case 'x':
		spec->flags |= SMALL;
		fallthrough;

	case 'X':
		spec->base = 16;
		break;

	case 'd':
	case 'i':
		spec->flags |= SIGN;
		break;
	case 'u':
		break;

	case 'n':
		
		fallthrough;

	default:
		WARN_ONCE(1, "Please remove unsupported %%%c in format string\n", *fmt);
		spec->type = FORMAT_TYPE_INVALID;
		return fmt - start;
	}

	if (qualifier == 'L')
		spec->type = FORMAT_TYPE_LONG_LONG;
	else if (qualifier == 'l') {
		BUILD_BUG_ON(FORMAT_TYPE_ULONG + SIGN != FORMAT_TYPE_LONG);
		spec->type = FORMAT_TYPE_ULONG + (spec->flags & SIGN);
	} else if (qualifier == 'z') {
		spec->type = FORMAT_TYPE_SIZE_T;
	} else if (qualifier == 't') {
		spec->type = FORMAT_TYPE_PTRDIFF;
	} else if (qualifier == 'H') {
		BUILD_BUG_ON(FORMAT_TYPE_UBYTE + SIGN != FORMAT_TYPE_BYTE);
		spec->type = FORMAT_TYPE_UBYTE + (spec->flags & SIGN);
	} else if (qualifier == 'h') {
		BUILD_BUG_ON(FORMAT_TYPE_USHORT + SIGN != FORMAT_TYPE_SHORT);
		spec->type = FORMAT_TYPE_USHORT + (spec->flags & SIGN);
	} else {
		BUILD_BUG_ON(FORMAT_TYPE_UINT + SIGN != FORMAT_TYPE_INT);
		spec->type = FORMAT_TYPE_UINT + (spec->flags & SIGN);
	}

	return ++fmt - start;
}

static void
set_field_width(struct printf_spec *spec, int width)
{
	spec->field_width = width;
	if (WARN_ONCE(spec->field_width != width, "field width %d too large", width)) {
		spec->field_width = clamp(width, -FIELD_WIDTH_MAX, FIELD_WIDTH_MAX);
	}
}

static void
set_precision(struct printf_spec *spec, int prec)
{
	spec->precision = prec;
	if (WARN_ONCE(spec->precision != prec, "precision %d too large", prec)) {
		spec->precision = clamp(prec, 0, PRECISION_MAX);
	}
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	unsigned long long num;
	char *str, *end;
	struct printf_spec spec = {0};

	
	if (WARN_ON_ONCE(size > INT_MAX))
		return 0;

	str = buf;
	end = buf + size;

	
	if (end < buf) {
		end = ((void *)-1);
		size = end - buf;
	}

	while (*fmt) {
		const char *old_fmt = fmt;
		int read = format_decode(fmt, &spec);

		fmt += read;

		switch (spec.type) {
		case FORMAT_TYPE_NONE: {
			int copy = read;
			if (str < end) {
				if (copy > end - str)
					copy = end - str;
				memcpy(str, old_fmt, copy);
			}
			str += read;
			break;
		}

		case FORMAT_TYPE_WIDTH:
			set_field_width(&spec, va_arg(args, int));
			break;

		case FORMAT_TYPE_PRECISION:
			set_precision(&spec, va_arg(args, int));
			break;

		case FORMAT_TYPE_CHAR: {
			char c;

			if (!(spec.flags & LEFT)) {
				while (--spec.field_width > 0) {
					if (str < end)
						*str = ' ';
					++str;

				}
			}
			c = (unsigned char) va_arg(args, int);
			if (str < end)
				*str = c;
			++str;
			while (--spec.field_width > 0) {
				if (str < end)
					*str = ' ';
				++str;
			}
			break;
		}

		case FORMAT_TYPE_STR:
			str = string(str, end, va_arg(args, char *), spec);
			break;

		case FORMAT_TYPE_PTR:
			str = pointer(fmt, str, end, va_arg(args, void *),
				      spec);
			while (isalnum(*fmt))
				fmt++;
			break;

		case FORMAT_TYPE_PERCENT_CHAR:
			if (str < end)
				*str = '%';
			++str;
			break;

		case FORMAT_TYPE_INVALID:
			
			goto out;

		default:
			switch (spec.type) {
			case FORMAT_TYPE_LONG_LONG:
				num = va_arg(args, long long);
				break;
			case FORMAT_TYPE_ULONG:
				num = va_arg(args, unsigned long);
				break;
			case FORMAT_TYPE_LONG:
				num = va_arg(args, long);
				break;
			case FORMAT_TYPE_SIZE_T:
				if (spec.flags & SIGN)
					num = va_arg(args, ssize_t);
				else
					num = va_arg(args, size_t);
				break;
			case FORMAT_TYPE_PTRDIFF:
				num = va_arg(args, ptrdiff_t);
				break;
			case FORMAT_TYPE_UBYTE:
				num = (unsigned char) va_arg(args, int);
				break;
			case FORMAT_TYPE_BYTE:
				num = (signed char) va_arg(args, int);
				break;
			case FORMAT_TYPE_USHORT:
				num = (unsigned short) va_arg(args, int);
				break;
			case FORMAT_TYPE_SHORT:
				num = (short) va_arg(args, int);
				break;
			case FORMAT_TYPE_INT:
				num = (int) va_arg(args, int);
				break;
			default:
				num = va_arg(args, unsigned int);
			}

			str = number(str, end, num, spec);
		}
	}

out:
	if (size > 0) {
		if (str < end)
			*str = '\0';
		else
			end[-1] = '\0';
	}

	
	return str-buf;

}

int vscnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	int i;

	if (unlikely(!size))
		return 0;

	i = vsnprintf(buf, size, fmt, args);

	if (likely(i < size))
		return i;

	return size - 1;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsnprintf(buf, size, fmt, args);
	va_end(args);

	return i;
}

int scnprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vscnprintf(buf, size, fmt, args);
	va_end(args);

	return i;
}

int vsprintf(char *buf, const char *fmt, va_list args)
{
	return vsnprintf(buf, INT_MAX, fmt, args);
}

int sprintf(char *buf, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsnprintf(buf, INT_MAX, fmt, args);
	va_end(args);

	return i;
}

int vsscanf(const char *buf, const char *fmt, va_list args)
{
	const char *str = buf;
	char *next;
	char digit;
	int num = 0;
	u8 qualifier;
	unsigned int base;
	union {
		long long s;
		unsigned long long u;
	} val;
	s16 field_width;
	bool is_sign;

	while (*fmt) {
		
		
		if (isspace(*fmt)) {
			fmt = skip_spaces(++fmt);
			str = skip_spaces(str);
		}

		
		if (*fmt != '%' && *fmt) {
			if (*fmt++ != *str++)
				break;
			continue;
		}

		if (!*fmt)
			break;
		++fmt;

		
		if (*fmt == '*') {
			if (!*str)
				break;
			while (!isspace(*fmt) && *fmt != '%' && *fmt) {
				
				if (*fmt == '[')
					return num;
				fmt++;
			}
			while (!isspace(*str) && *str)
				str++;
			continue;
		}

		
		field_width = -1;
		if (isdigit(*fmt)) {
			field_width = skip_atoi(&fmt);
			if (field_width <= 0)
				break;
		}

		
		qualifier = -1;
		if (*fmt == 'h' || _tolower(*fmt) == 'l' ||
		    *fmt == 'z') {
			qualifier = *fmt++;
			if (unlikely(qualifier == *fmt)) {
				if (qualifier == 'h') {
					qualifier = 'H';
					fmt++;
				} else if (qualifier == 'l') {
					qualifier = 'L';
					fmt++;
				}
			}
		}

		if (!*fmt)
			break;

		if (*fmt == 'n') {
			
			*va_arg(args, int *) = str - buf;
			++fmt;
			continue;
		}

		if (!*str)
			break;

		base = 10;
		is_sign = false;

		switch (*fmt++) {
		case 'c':
		{
			char *s = (char *)va_arg(args, char*);
			if (field_width == -1)
				field_width = 1;
			do {
				*s++ = *str++;
			} while (--field_width > 0 && *str);
			num++;
		}
		continue;
		case 's':
		{
			char *s = (char *)va_arg(args, char *);
			if (field_width == -1)
				field_width = SHRT_MAX;
			
			str = skip_spaces(str);

			
			while (*str && !isspace(*str) && field_width--)
				*s++ = *str++;
			*s = '\0';
			num++;
		}
		continue;
		
		case '[':
		{
			char *s = (char *)va_arg(args, char *);
			DECLARE_BITMAP(set, 256) = {0};
			unsigned int len = 0;
			bool negate = (*fmt == '^');

			
			if (field_width == -1)
				return num;

			if (negate)
				++fmt;

			for ( ; *fmt && *fmt != ']'; ++fmt, ++len)
				__set_bit((u8)*fmt, set);

			
			if (!*fmt || !len)
				return num;
			++fmt;

			if (negate) {
				bitmap_complement(set, set, 256);
				
				__clear_bit(0, set);
			}

			
			if (!test_bit((u8)*str, set))
				return num;

			while (test_bit((u8)*str, set) && field_width--)
				*s++ = *str++;
			*s = '\0';
			++num;
		}
		continue;
		case 'o':
			base = 8;
			break;
		case 'x':
		case 'X':
			base = 16;
			break;
		case 'i':
			base = 0;
			fallthrough;
		case 'd':
			is_sign = true;
			fallthrough;
		case 'u':
			break;
		case '%':
			
			if (*str++ != '%')
				return num;
			continue;
		default:
			
			return num;
		}

		
		str = skip_spaces(str);

		digit = *str;
		if (is_sign && digit == '-') {
			if (field_width == 1)
				break;

			digit = *(str + 1);
		}

		if (!digit
		    || (base == 16 && !isxdigit(digit))
		    || (base == 10 && !isdigit(digit))
		    || (base == 8 && (!isdigit(digit) || digit > '7'))
		    || (base == 0 && !isdigit(digit)))
			break;

		if (is_sign)
			val.s = simple_strntoll(str,
						field_width >= 0 ? field_width : INT_MAX,
						&next, base);
		else
			val.u = simple_strntoull(str,
						 field_width >= 0 ? field_width : INT_MAX,
						 &next, base);

		switch (qualifier) {
		case 'H':	
			if (is_sign)
				*va_arg(args, signed char *) = val.s;
			else
				*va_arg(args, unsigned char *) = val.u;
			break;
		case 'h':
			if (is_sign)
				*va_arg(args, short *) = val.s;
			else
				*va_arg(args, unsigned short *) = val.u;
			break;
		case 'l':
			if (is_sign)
				*va_arg(args, long *) = val.s;
			else
				*va_arg(args, unsigned long *) = val.u;
			break;
		case 'L':
			if (is_sign)
				*va_arg(args, long long *) = val.s;
			else
				*va_arg(args, unsigned long long *) = val.u;
			break;
		case 'z':
			*va_arg(args, size_t *) = val.u;
			break;
		default:
			if (is_sign)
				*va_arg(args, int *) = val.s;
			else
				*va_arg(args, unsigned int *) = val.u;
			break;
		}
		num++;

		if (!next)
			break;
		str = next;
	}

	return num;
}

int sscanf(const char *buf, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsscanf(buf, fmt, args);
	va_end(args);

	return i;
}
