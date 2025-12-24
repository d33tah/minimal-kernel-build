
#include "xz_private.h"

#ifdef XZ_DEC_BCJ

struct xz_dec_bcj {
	enum { BCJ_X86 = 4 } type;

	enum xz_ret ret;

	bool single_call;

	uint32_t pos;

	uint32_t x86_prev_mask;

	uint8_t *out;
	size_t out_pos;
	size_t out_size;

	struct {
		size_t filtered;

		size_t size;

		uint8_t buf[16];
	} temp;
};

#ifdef XZ_DEC_X86
static inline int bcj_x86_test_msbyte(uint8_t b)
{
	return b == 0x00 || b == 0xFF;
}

static size_t bcj_x86(struct xz_dec_bcj *s, uint8_t *buf, size_t size)
{
	static const bool mask_to_allowed_status[8] = { true,  true, true,
							false, true, false,
							false, false };

	static const uint8_t mask_to_bit_num[8] = { 0, 1, 2, 2, 3, 3, 3, 3 };

	size_t i;
	size_t prev_pos = (size_t)-1;
	uint32_t prev_mask = s->x86_prev_mask;
	uint32_t src;
	uint32_t dest;
	uint32_t j;
	uint8_t b;

	if (size <= 4)
		return 0;

	size -= 4;
	for (i = 0; i < size; ++i) {
		if ((buf[i] & 0xFE) != 0xE8)
			continue;

		prev_pos = i - prev_pos;
		if (prev_pos > 3) {
			prev_mask = 0;
		} else {
			prev_mask = (prev_mask << (prev_pos - 1)) & 7;
			if (prev_mask != 0) {
				b = buf[i + 4 - mask_to_bit_num[prev_mask]];
				if (!mask_to_allowed_status[prev_mask] ||
				    bcj_x86_test_msbyte(b)) {
					prev_pos = i;
					prev_mask = (prev_mask << 1) | 1;
					continue;
				}
			}
		}

		prev_pos = i;

		if (bcj_x86_test_msbyte(buf[i + 4])) {
			src = get_unaligned_le32(buf + i + 1);
			while (true) {
				dest = src - (s->pos + (uint32_t)i + 5);
				if (prev_mask == 0)
					break;

				j = mask_to_bit_num[prev_mask] * 8;
				b = (uint8_t)(dest >> (24 - j));
				if (!bcj_x86_test_msbyte(b))
					break;

				src = dest ^ (((uint32_t)1 << (32 - j)) - 1);
			}

			dest &= 0x01FFFFFF;
			dest |= (uint32_t)0 - (dest & 0x01000000);
			put_unaligned_le32(dest, buf + i + 1);
			i += 4;
		} else {
			prev_mask = (prev_mask << 1) | 1;
		}
	}

	prev_pos = i - prev_pos;
	s->x86_prev_mask = prev_pos > 3 ? 0 : prev_mask << (prev_pos - 1);
	return i;
}
#endif

static void bcj_apply(struct xz_dec_bcj *s, uint8_t *buf, size_t *pos,
		      size_t size)
{
	size_t filtered;

	buf += *pos;
	size -= *pos;

	switch (s->type) {
#ifdef XZ_DEC_X86
	case BCJ_X86:
		filtered = bcj_x86(s, buf, size);
		break;
#endif
	default:

		filtered = 0;
		break;
	}

	*pos += filtered;
	s->pos += filtered;
}

static void bcj_flush(struct xz_dec_bcj *s, struct xz_buf *b)
{
	size_t copy_size;

	copy_size = min_t(size_t, s->temp.filtered, b->out_size - b->out_pos);
	memcpy(b->out + b->out_pos, s->temp.buf, copy_size);
	b->out_pos += copy_size;

	s->temp.filtered -= copy_size;
	s->temp.size -= copy_size;
	memmove(s->temp.buf, s->temp.buf + copy_size, s->temp.size);
}

XZ_EXTERN enum xz_ret xz_dec_bcj_run(struct xz_dec_bcj *s,
				     struct xz_dec_lzma2 *lzma2,
				     struct xz_buf *b)
{
	size_t out_start;

	if (s->temp.filtered > 0) {
		bcj_flush(s, b);
		if (s->temp.filtered > 0)
			return XZ_OK;

		if (s->ret == XZ_STREAM_END)
			return XZ_STREAM_END;
	}

	if (s->temp.size < b->out_size - b->out_pos || s->temp.size == 0) {
		out_start = b->out_pos;
		memcpy(b->out + b->out_pos, s->temp.buf, s->temp.size);
		b->out_pos += s->temp.size;

		s->ret = xz_dec_lzma2_run(lzma2, b);
		if (s->ret != XZ_STREAM_END &&
		    (s->ret != XZ_OK || s->single_call))
			return s->ret;

		bcj_apply(s, b->out, &out_start, b->out_pos);

		if (s->ret == XZ_STREAM_END)
			return XZ_STREAM_END;

		s->temp.size = b->out_pos - out_start;
		b->out_pos -= s->temp.size;
		memcpy(s->temp.buf, b->out + b->out_pos, s->temp.size);

		if (b->out_pos + s->temp.size < b->out_size)
			return XZ_OK;
	}

	if (b->out_pos < b->out_size) {
		s->out = b->out;
		s->out_pos = b->out_pos;
		s->out_size = b->out_size;
		b->out = s->temp.buf;
		b->out_pos = s->temp.size;
		b->out_size = sizeof(s->temp.buf);

		s->ret = xz_dec_lzma2_run(lzma2, b);

		s->temp.size = b->out_pos;
		b->out = s->out;
		b->out_pos = s->out_pos;
		b->out_size = s->out_size;

		if (s->ret != XZ_OK && s->ret != XZ_STREAM_END)
			return s->ret;

		bcj_apply(s, s->temp.buf, &s->temp.filtered, s->temp.size);

		if (s->ret == XZ_STREAM_END)
			s->temp.filtered = s->temp.size;

		bcj_flush(s, b);
		if (s->temp.filtered > 0)
			return XZ_OK;
	}

	return s->ret;
}

XZ_EXTERN struct xz_dec_bcj *xz_dec_bcj_create(bool single_call)
{
	struct xz_dec_bcj *s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (s != NULL)
		s->single_call = single_call;

	return s;
}

XZ_EXTERN enum xz_ret xz_dec_bcj_reset(struct xz_dec_bcj *s, uint8_t id)
{
	switch (id) {
#ifdef XZ_DEC_X86
	case BCJ_X86:
#endif
		break;

	default:

		return XZ_OPTIONS_ERROR;
	}

	s->type = id;
	s->ret = XZ_OK;
	s->pos = 0;
	s->x86_prev_mask = 0;
	s->temp.filtered = 0;
	s->temp.size = 0;

	return XZ_OK;
}

#endif
