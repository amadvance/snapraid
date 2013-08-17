/*
 * Copyright (C) 2011 Andrea Mazzoleni
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "portable.h"

#include "util.h"

/****************************************************************************/
/* hex conversion table */

static char strhexset[16] = "0123456789abcdef";

static unsigned strdecset[256] =
{
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1, -1, -1, -1, -1, -1,
	-1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

/****************************************************************************/
/* stream */

STREAM* sopen_read(const char* file)
{
#if HAVE_POSIX_FADVISE
	int ret;
#endif
	STREAM* s = malloc_nofail(sizeof(STREAM));

	s->handle_size = 1;
	s->handle = malloc_nofail(sizeof(struct stream_handle));

	pathcpy(s->handle[0].path, sizeof(s->handle[0].path), file);
	s->handle[0].f = open(file, O_RDONLY | O_BINARY | O_SEQUENTIAL);
	if (s->handle[0].f == -1) {
		free(s->handle);
		free(s);
		return 0;
	}

#if HAVE_POSIX_FADVISE
	/* advise sequential access */
	ret = posix_fadvise(s->handle[0].f, 0, 0, POSIX_FADV_SEQUENTIAL);
	if (ret != 0) {
		close(s->handle[0].f);
		free(s->handle);
		free(s);
		return 0;
	}
#endif

	s->buffer = malloc_nofail(STREAM_SIZE);
	s->pos = s->buffer;
	s->end = s->buffer;
	s->state = STREAM_STATE_READ;
	s->state_index = 0;
	s->offset = 0;
	s->offset_uncached = 0;
	s->crc = 0;
	s->crc_uncached = 0;

	return s;
}

STREAM* sopen_multi_write(unsigned count)
{
	unsigned i;

	STREAM* s = malloc_nofail(sizeof(STREAM));

	s->handle_size = count;
	s->handle = malloc_nofail(count * sizeof(struct stream_handle));

	for(i=0;i<count;++i)
		s->handle[i].f = -1;

	s->buffer = malloc_nofail(STREAM_SIZE);
	s->pos = s->buffer;
	s->end = s->buffer + STREAM_SIZE;
	s->state = STREAM_STATE_WRITE;
	s->state_index = 0;
	s->offset = 0;
	s->offset_uncached = 0;
	s->crc = 0;
	s->crc_uncached = 0;

	return s;
}

int sopen_multi_file(STREAM* s, unsigned i, const char* file)
{
#if HAVE_POSIX_FADVISE
	int ret;
#endif
	int f;

	pathcpy(s->handle[i].path, sizeof(s->handle[i].path), file);

	f = open(file, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY | O_SEQUENTIAL, 0600);
	if (f == -1) {
		return -1;
	}

#if HAVE_POSIX_FADVISE
	/* advise sequential access */
	ret = posix_fadvise(f, 0, 0, POSIX_FADV_SEQUENTIAL);
	if (ret != 0) {
		close(f);
		return -1;
	}
#endif

	s->handle[i].f = f;

	return 0;
}

int sclose(STREAM* s)
{
	int fail = 0;
	unsigned i;

	if (s->state == STREAM_STATE_WRITE) {
		if (sflush(s) != 0)
			fail = 1;
	}

	for(i=0;i<s->handle_size;++i) {
		if (close(s->handle[i].f) != 0)
			fail = 1;
	}

	free(s->handle);
	free(s->buffer);
	free(s);

	if (fail)
		return -1;

	return 0;
}

int shandle(STREAM* s)
{
	if (!s->handle_size)
		return -1;

	return s->handle[0].f;
}

int sfill(STREAM* s)
{
	ssize_t ret;

	if (s->state != STREAM_STATE_READ)
		return EOF;

	ret = read(s->handle[0].f, s->buffer, STREAM_SIZE);

	if (ret < 0) {
		s->state = STREAM_STATE_ERROR;
		return EOF;
	}
	if (ret == 0) {
		s->state = STREAM_STATE_EOF;
		return EOF;
	}

	/* update the crc */
	s->crc_uncached = s->crc;
	s->crc = crc32c(s->crc, s->buffer, ret);

	/* update the offset */
	s->offset_uncached = s->offset;
	s->offset += ret;

	s->pos = s->buffer;
	s->end = s->buffer + ret;

	return *s->pos++;
}

int sflush(STREAM* s)
{
	ssize_t ret;
	ssize_t size;
	unsigned i;

	if (s->state != STREAM_STATE_WRITE)
		return EOF;

	size = s->pos - s->buffer;
	if (!size)
		return 0;

	/* update the crc */
	s->crc = crc32c(s->crc, s->buffer, size);
	s->crc_uncached = s->crc;

	for(i=0;i<s->handle_size;++i) {
		ret = write(s->handle[i].f, s->buffer, size);

		if (ret != size) {
			s->state = STREAM_STATE_ERROR;
			s->state_index = i;
			return EOF;
		}
	}

	/* update the offset */
	s->offset += size;
	s->offset_uncached = s->offset;

	s->pos = s->buffer;

	return 0;
}

int64_t stell(STREAM* s)
{
	return s->offset_uncached + (s->pos - s->buffer);
}

uint32_t scrc(STREAM*s)
{
	return crc32c(s->crc_uncached, s->buffer, s->pos - s->buffer);
}

int sgettok(STREAM* f, char* str, int size)
{
	char* i = str;
	char* send = str + size;
	int c;

	while (1) {
		c = sgetc(f);
		if (c == EOF) {
			break;
		}
		if (c == ' ' || c == '\t') {
			sungetc(c, f);
			break;
		}
		if (c == '\n') {
			/* remove ending carrige return to support the Windows CR+LF format */
			if (i != str && i[-1] == '\r')
				--i;
			sungetc(c, f);
			break;
		}

		*i++ = c;

		if (i == send)
			return -1;
	}

	*i = 0;

	return i - str;
}

int sread(STREAM* f, void* void_data, unsigned size)
{
	unsigned char* data = void_data;

	/* if there is enough space in memory */
	if (sptrlookup(f, size)) {
		/* optimized version with all the data in memory */
		unsigned char* pos = sptrget(f);

		/* copy it */
		while (size--) {
			*data++ = *pos++;
		}

		sptrset(f, pos);
	} else {
		/* standard version using sputc() */
		while (size--) {
			int c = sgetc(f);
			if (c == EOF)
				return -1;

			*data++ = c;
		}
	}

	return 0;
}

int sgetline(STREAM* f, char* str, int size)
{
	char* i = str;
	char* send = str + size;
	int c;

	/* if there is enough data in memory */
	if (sptrlookup(f, size)) {
		/* optimized version with all the data in memory */
		unsigned char* pos = sptrget(f);

		while (1) {
			c = *pos++;
			if (c == '\n') {
				/* remove ending carrige return to support the Windows CR+LF format */
				if (i != str && i[-1] == '\r')
					--i;
				--pos;
				break;
			}

			*i++ = c;

			if (i == send)
				return -1;
		}

		sptrset(f, pos);
	} else {
		while (1) {
			c = sgetc(f);
			if (c == EOF) {
				break;
			}
			if (c == '\n') {
				/* remove ending carrige return to support the Windows CR+LF format */
				if (i != str && i[-1] == '\r')
					--i;
				sungetc(c, f);
				break;
			}

			*i++ = c;

			if (i == send)
				return -1;
		}
	}

	*i = 0;

	return i - str;
}

int sgetlasttok(STREAM* f, char* str, int size)
{
	int ret;

	ret = sgetline(f, str, size);
	if (ret < 0)
		return ret;

	while (ret > 0 && (str[ret-1] == ' ' || str[ret-1] == '\t')) {
		--ret;
	}

	str[ret] = 0;

	return ret;
}

int sgetu32(STREAM* f, uint32_t* value)
{
	int c;

	c = sgetc(f);
	if (c>='0' && c<='9') {
		uint32_t v;

		v = c - '0';

		c = sgetc(f);
		while (c>='0' && c<='9') {
			v *= 10;
			v += c - '0';
			c = sgetc(f);
		}

		*value = v;

		sungetc(c, f);
		return 0;
	} else {
		/* nothing read */
		return -1;
	}
}

int sgetu64(STREAM* f, uint64_t* value)
{
	int c;

	c = sgetc(f);
	if (c>='0' && c<='9') {
		uint64_t v;

		v = c - '0';

		c = sgetc(f);
		while (c>='0' && c<='9') {
			v *= 10;
			v += c - '0';
			c = sgetc(f);
		}

		*value = v;

		sungetc(c, f);
		return 0;
	} else {
		/* nothing read */
		return -1;
	}
}

int sgethex(STREAM* f, void* void_data, int size)
{
	unsigned char* data = void_data;

	/* if there is enough data in memory */
	if (sptrlookup(f, size * 2)) {
		/* optimized version with all the data in memory */
		unsigned char* pos = sptrget(f);
		unsigned x = 0;

		while (size--) {
			unsigned b0;
			unsigned b1;
			unsigned b;

			b0 = strdecset[pos[0]];
			b1 = strdecset[pos[1]];
			pos += 2;

			b = (b0 << 4) | b1;

			x |= b;

			*data++ = b;
		}

		/* at the end check if a digit was wrong */
		if (x > 0xFF) {
			return -1;
		}

		sptrset(f, pos);
	} else {
		/* standard version using sgetc() */
		while (size--) {
			unsigned b0;
			unsigned b1;
			unsigned b;
			int c;

			c = sgetc(f);
			if (c == EOF)
				return -1;
			b0 = strdecset[c];

			c = sgetc(f);
			if (c == EOF)
				return -1;
			b1 = strdecset[c];

			b = (b0 << 4) | b1;

			if (b > 0xFF)
				return -1;

			*data++ = b;
		}
	}

	return 0;
}

int sgetb32(STREAM* f, uint32_t* value)
{
	uint32_t v;
	unsigned char b;
	unsigned char s;
	int c;

	v = 0;
	s = 0;
loop:
	c = sgetc(f);
	if (c == EOF)
		return -1;

	b = (unsigned char)c;
	if ((b & 0x80) == 0) {
		v |= (uint32_t)b << s;
		s += 7;
		goto loop;
	}

	v |= (uint32_t)(b & 0x7f) << s;

	*value = v;

	return 0;
}

int sgetb64(STREAM* f, uint64_t* value)
{
	uint64_t v;
	unsigned char b;
	unsigned char s;
	int c;

	v = 0;
	s = 0;
loop:
	c = sgetc(f);
	if (c == EOF)
		return -1;

	b = (unsigned char)c;
	if ((b & 0x80) == 0) {
		v |= (uint64_t)b << s;
		s += 7;
		goto loop;
	}

	v |= (uint64_t)(b & 0x7f) << s;

	*value = v;

	return 0;
}

int sgetble32(STREAM* f, uint32_t* value)
{
	unsigned char buf[4];

	if (sread(f, buf, 4) != 0)
		return -1;

	*value = buf[0] | (uint32_t)buf[1] << 8 | (uint32_t)buf[2] << 16 | (uint32_t)buf[3] << 24;

	return 0;
}

int sgetbs(STREAM* f, char* str, int size)
{
	uint32_t len;

	if (sgetb32(f, &len) < 0)
		return -1;

	if (len + 1 > (uint32_t)size)
		return -1;

	str[len] = 0;

	return sread(f, str, (int)len);
}

int sputs(const char* str, STREAM* f)
{
	while (*str) {
		if (sputc(*str++, f) != 0)
			return -1;
	}

	return 0;
}

int swrite(const void* void_data, unsigned size, STREAM* f)
{
	const unsigned char* data = void_data;

	/* if there is enough space in memory */
	if (sptrlookup(f, size)) {
		/* optimized version with all the data in memory */
		unsigned char* pos = sptrget(f);

		/* copy it */
		while (size--) {
			*pos++ = *data++;
		}

		sptrset(f, pos);
	} else {
		/* standard version using sputc() */
		while (size--) {
			if (sputc(*data++, f) != 0)
				return -1;
		}
	}

	return 0;
}

int sputu32(uint32_t value, STREAM* s)
{
	char buf[16];
	int i;

	if (!value)
		return sputc('0', s);

	i = sizeof(buf);

	while (value) {
		buf[--i] = (value % 10) + '0';
		value /= 10;
	}

	return swrite(buf + i, sizeof(buf) - i, s);
}

int sputu64(uint64_t value, STREAM* s)
{
	char buf[32];
	uint32_t value32;
	int i;

	if (!value)
		return sputc('0', s);

	i = sizeof(buf);

	while (value > 0xFFFFFFFF) {
		buf[--i] = (value % 10) + '0';
		value /= 10;
	}

	value32 = (uint32_t)value;

	while (value32) {
		buf[--i] = (value32 % 10) + '0';
		value32 /= 10;
	}

	return swrite(buf + i, sizeof(buf) - i, s);
}

int sputhex(const void* void_data, int size, STREAM* f)
{
	const unsigned char* data = void_data;

	/* if there is enough space in memory */
	if (sptrlookup(f, size * 2)) {
		/* optimized version with all the data in memory */
		unsigned char* pos = sptrget(f);

		while (size) {
			unsigned b = *data;

			*pos++ = strhexset[b >> 4];
			*pos++ = strhexset[b & 0xF];

			++data;
			--size;
		}

		sptrset(f, pos);
	} else {
		/* standard version using sputc() */
		while (size) {
			unsigned b = *data;

			if (sputc(strhexset[b >> 4], f) != 0)
				return -1;
			if (sputc(strhexset[b & 0xF], f) != 0)
				return -1;

			++data;
			--size;
		}
	}

	return 0;
}

int sputb32(uint32_t value, STREAM* s)
{
	unsigned char b;
	unsigned char buf[16];
	unsigned i;

	i = 0;
loop:
	b = value & 0x7f;
	value >>= 7;

	if (value) {
		buf[i++] = b;
		goto loop;
	}

	buf[i++] = b | 0x80;

	return swrite(buf, i, s);
}

int sputb64(uint64_t value, STREAM* s)
{
	unsigned char b;
	unsigned char buf[16];
	unsigned i;

	i = 0;
loop:
	b = value & 0x7f;
	value >>= 7;

	if (value) {
		buf[i++] = b;
		goto loop;
	}

	buf[i++] = b | 0x80;

	return swrite(buf, i, s);
}

int sputble32(uint32_t value, STREAM* s)
{
	unsigned char buf[4];

	buf[0] = value & 0xFF;
	buf[1] = (value >> 8) & 0xFF;
	buf[2] = (value >> 16) & 0xFF;
	buf[3] = (value >> 24) & 0xFF;

	return swrite(buf, 4, s);
}

int sputbs(const char* str, STREAM* f)
{
	size_t len = strlen(str);

	if (sputb32(len, f) != 0)
		return -1;

	return swrite(str, len, f);
}

#if HAVE_FSYNC
int ssync(STREAM* s)
{
	unsigned i;

	for(i=0;i<s->handle_size;++i) {
		if (fsync(s->handle[i].f) != 0) {
			s->state = STREAM_STATE_ERROR;
			s->state_index = i;
			return -1;
		}
	}

	return 0;
}
#endif

/****************************************************************************/
/* crc */

static uint32_t CRC32C[256] = {
	0x00000000, 0xf26b8303, 0xe13b70f7, 0x1350f3f4,
	0xc79a971f, 0x35f1141c, 0x26a1e7e8, 0xd4ca64eb,
	0x8ad958cf, 0x78b2dbcc, 0x6be22838, 0x9989ab3b,
	0x4d43cfd0, 0xbf284cd3, 0xac78bf27, 0x5e133c24,
	0x105ec76f, 0xe235446c, 0xf165b798, 0x030e349b,
	0xd7c45070, 0x25afd373, 0x36ff2087, 0xc494a384,
	0x9a879fa0, 0x68ec1ca3, 0x7bbcef57, 0x89d76c54,
	0x5d1d08bf, 0xaf768bbc, 0xbc267848, 0x4e4dfb4b,
	0x20bd8ede, 0xd2d60ddd, 0xc186fe29, 0x33ed7d2a,
	0xe72719c1, 0x154c9ac2, 0x061c6936, 0xf477ea35,
	0xaa64d611, 0x580f5512, 0x4b5fa6e6, 0xb93425e5,
	0x6dfe410e, 0x9f95c20d, 0x8cc531f9, 0x7eaeb2fa,
	0x30e349b1, 0xc288cab2, 0xd1d83946, 0x23b3ba45,
	0xf779deae, 0x05125dad, 0x1642ae59, 0xe4292d5a,
	0xba3a117e, 0x4851927d, 0x5b016189, 0xa96ae28a,
	0x7da08661, 0x8fcb0562, 0x9c9bf696, 0x6ef07595,
	0x417b1dbc, 0xb3109ebf, 0xa0406d4b, 0x522bee48,
	0x86e18aa3, 0x748a09a0, 0x67dafa54, 0x95b17957,
	0xcba24573, 0x39c9c670, 0x2a993584, 0xd8f2b687,
	0x0c38d26c, 0xfe53516f, 0xed03a29b, 0x1f682198,
	0x5125dad3, 0xa34e59d0, 0xb01eaa24, 0x42752927,
	0x96bf4dcc, 0x64d4cecf, 0x77843d3b, 0x85efbe38,
	0xdbfc821c, 0x2997011f, 0x3ac7f2eb, 0xc8ac71e8,
	0x1c661503, 0xee0d9600, 0xfd5d65f4, 0x0f36e6f7,
	0x61c69362, 0x93ad1061, 0x80fde395, 0x72966096,
	0xa65c047d, 0x5437877e, 0x4767748a, 0xb50cf789,
	0xeb1fcbad, 0x197448ae, 0x0a24bb5a, 0xf84f3859,
	0x2c855cb2, 0xdeeedfb1, 0xcdbe2c45, 0x3fd5af46,
	0x7198540d, 0x83f3d70e, 0x90a324fa, 0x62c8a7f9,
	0xb602c312, 0x44694011, 0x5739b3e5, 0xa55230e6,
	0xfb410cc2, 0x092a8fc1, 0x1a7a7c35, 0xe811ff36,
	0x3cdb9bdd, 0xceb018de, 0xdde0eb2a, 0x2f8b6829,
	0x82f63b78, 0x709db87b, 0x63cd4b8f, 0x91a6c88c,
	0x456cac67, 0xb7072f64, 0xa457dc90, 0x563c5f93,
	0x082f63b7, 0xfa44e0b4, 0xe9141340, 0x1b7f9043,
	0xcfb5f4a8, 0x3dde77ab, 0x2e8e845f, 0xdce5075c,
	0x92a8fc17, 0x60c37f14, 0x73938ce0, 0x81f80fe3,
	0x55326b08, 0xa759e80b, 0xb4091bff, 0x466298fc,
	0x1871a4d8, 0xea1a27db, 0xf94ad42f, 0x0b21572c,
	0xdfeb33c7, 0x2d80b0c4, 0x3ed04330, 0xccbbc033,
	0xa24bb5a6, 0x502036a5, 0x4370c551, 0xb11b4652,
	0x65d122b9, 0x97baa1ba, 0x84ea524e, 0x7681d14d,
	0x2892ed69, 0xdaf96e6a, 0xc9a99d9e, 0x3bc21e9d,
	0xef087a76, 0x1d63f975, 0x0e330a81, 0xfc588982,
	0xb21572c9, 0x407ef1ca, 0x532e023e, 0xa145813d,
	0x758fe5d6, 0x87e466d5, 0x94b49521, 0x66df1622,
	0x38cc2a06, 0xcaa7a905, 0xd9f75af1, 0x2b9cd9f2,
	0xff56bd19, 0x0d3d3e1a, 0x1e6dcdee, 0xec064eed,
	0xc38d26c4, 0x31e6a5c7, 0x22b65633, 0xd0ddd530,
	0x0417b1db, 0xf67c32d8, 0xe52cc12c, 0x1747422f,
	0x49547e0b, 0xbb3ffd08, 0xa86f0efc, 0x5a048dff,
	0x8ecee914, 0x7ca56a17, 0x6ff599e3, 0x9d9e1ae0,
	0xd3d3e1ab, 0x21b862a8, 0x32e8915c, 0xc083125f,
	0x144976b4, 0xe622f5b7, 0xf5720643, 0x07198540,
	0x590ab964, 0xab613a67, 0xb831c993, 0x4a5a4a90,
	0x9e902e7b, 0x6cfbad78, 0x7fab5e8c, 0x8dc0dd8f,
	0xe330a81a, 0x115b2b19, 0x020bd8ed, 0xf0605bee,
	0x24aa3f05, 0xd6c1bc06, 0xc5914ff2, 0x37faccf1,
	0x69e9f0d5, 0x9b8273d6, 0x88d28022, 0x7ab90321,
	0xae7367ca, 0x5c18e4c9, 0x4f48173d, 0xbd23943e,
	0xf36e6f75, 0x0105ec76, 0x12551f82, 0xe03e9c81,
	0x34f4f86a, 0xc69f7b69, 0xd5cf889d, 0x27a40b9e,
	0x79b737ba, 0x8bdcb4b9, 0x988c474d, 0x6ae7c44e,
	0xbe2da0a5, 0x4c4623a6, 0x5f16d052, 0xad7d5351
};

uint32_t crc32c(uint32_t crc, const unsigned char* ptr, unsigned size)
{
	unsigned i;

	for(i=0;i<size;++i)
		crc = CRC32C[(crc ^ ptr[i]) & 0xff] ^ (crc >> 8);

	return crc;
}

/****************************************************************************/
/* path */

void pathcpy(char* dst, size_t size, const char* src)
{
	size_t len = strlen(src);

	if (len + 1 > size) {
		fprintf(stderr, "Path too long\n");
		exit(EXIT_FAILURE);
	}

	memcpy(dst, src, len + 1);
}

void pathcat(char* dst, size_t size, const char* src)
{
	size_t dst_len = strlen(dst);
	size_t src_len = strlen(src);

	if (dst_len + src_len + 1 > size) {
		fprintf(stderr, "Path too long\n");
		exit(EXIT_FAILURE);
	}

	memcpy(dst + dst_len, src, src_len + 1);
}

void pathimport(char* dst, size_t size, const char* src)
{
	pathcpy(dst, size, src);

#ifdef _WIN32
	/* convert all Windows '\' to '/' */
	while (*dst) {
		if (*dst == '\\')
			*dst = '/';
		++dst;
	}
#endif
}

void pathprint(char* dst, size_t size, const char* format, ...)
{
	size_t len;
	va_list ap;
	
	va_start(ap, format);
	len = vsnprintf(dst, size, format, ap);
	va_end(ap);

	if (len >= size) {
		fprintf(stderr, "Path too long\n");
		exit(EXIT_FAILURE);
	}
}

void pathslash(char* dst, size_t size)
{
	size_t len = strlen(dst);

	if (len > 0 && dst[len - 1] != '/') {
		if (len + 2 >= size) {
			fprintf(stderr, "Path too long\n");
			exit(EXIT_FAILURE);
		}

		dst[len] = '/';
		dst[len+1] = 0;
	}
}

int pathcmp(const char* a, const char* b)
{
#ifdef _WIN32
	char ai[PATH_MAX];
	char bi[PATH_MAX];

	/* import to convert \ to / */
	pathimport(ai, sizeof(ai), a);
	pathimport(bi, sizeof(bi), b);

	/* case insensitive compare in Windows */
	return stricmp(ai, bi);
#else
	return strcmp(a, b);
#endif
}

/****************************************************************************/
/* filesystem */

int mkancestor(const char* file)
{
	char dir[PATH_MAX];
	char* c;

	pathcpy(dir, sizeof(dir), file);

	c = strrchr(dir, '/');
	if (!c) {
		/* no ancestor */
		return 0;
	}

	/* clear the file */
	*c = 0;

	/* if it's the root dir */
	if (*dir == 0) {
		/* nothing more to do */
		return 0;
	}

#ifdef _WIN32
	/* if it's a drive specificaion like "C:" */
	if (isalpha(dir[0]) && dir[1] == ':' && dir[2] == 0) {
		/* nothing more to do */
		return 0;
	}
#endif

	/* try creating it  */
	if (mkdir(dir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == 0) {
		/* just created */
		return 0;
	}
	if (errno == EEXIST) {
		/* it already exists */
		return 0;
	}

	/* recursively create them all */
	if (mkancestor(dir) != 0) {
		return -1;
	}

	/* create it */
	if (mkdir(dir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
		fprintf(stderr, "Error creating directory '%s'. %s.\n", dir, strerror(errno));
		return -1;
	}

	return 0;
}

/****************************************************************************/
/* mem */

static size_t mcounter;

size_t malloc_counter(void)
{
	return mcounter;
}

void* malloc_nofail(size_t size)
{
	void* ptr = malloc(size);

	if (!ptr) {
		/* don't use printf to avoid any possible extra allocation */
		write(2, "Low Memory\n", 11);
		exit(EXIT_FAILURE);
	}

#ifndef CHECKER /* Don't preinitialize when running for valgrind */
	/* Here we preinitialize the memory to ensure that the OS is really allocating it */
	/* and not only reserving the addressable space. */
	/* Otherwise we are risking that the OOM (Out Of Memory) killer in Linux will kill the process. */
	/* Filling the memory doesn't ensure to disable OOM, but it increase a lot the chances to */
	/* get a real error from malloc() instead than a process killed. */
	/* Note that calloc() doesn't have the same effect. */
	memset(ptr, 0xA5, size);
#endif

	mcounter += size;

	return ptr;
}

#define ALIGN 256

void* malloc_nofail_align(size_t size, void** freeptr)
{
	unsigned char* ptr = malloc_nofail(size + ALIGN);
	uintptr_t offset;

	*freeptr = ptr;

	offset = ((uintptr_t)ptr) % ALIGN;

	if (offset != 0) {
		ptr += ALIGN - offset;
	}

	return ptr;
}

char* strdup_nofail(const char* str)
{
	size_t size;
	char* ptr;

	size = strlen(str) + 1;

	ptr = malloc(size);

	if (!ptr) {
		/* don't use printf to avoid any possible extra allocation */
		write(2, "Low Memory\n", 11);
		exit(EXIT_FAILURE);
	}

	memcpy(ptr, str, size);

	mcounter += size;

	return ptr;
}

/****************************************************************************/
/* hash */

/* Rotate left 32 */
inline uint32_t rotl32(uint32_t x, int8_t r)
{
	return (x << r) | (x >> (32 - r));
}

inline uint64_t rotl64(uint64_t x, int8_t r)
{
	return (x << r) | (x >> (64 - r));
}

/* Swap endianess */
#if HAVE_BYTESWAP_H

#include <byteswap.h>

#define swap32(x) bswap_32(x)
#define swap64(x) bswap_64(x)

#else
static inline uint32_t swap32(uint32_t v)
{
	return (rotl32(v, 8) & 0x00ff00ff)
		| (rotl32(v, 24) & 0xff00ff00);
}

static inline uint64_t swap64(uint64_t v)
{
	return (rotl64(v, 8) & 0x000000ff000000ffLLU)
		| (rotl64(v, 24) & 0x0000ff000000ff00LLU)
		| (rotl64(v, 40) & 0x00ff000000ff0000LLU)
		| (rotl64(v, 56) & 0xff000000ff000000LLU);
}
#endif

#include "murmur3.c"
#include "spooky2.c"

void memhash(unsigned kind, const unsigned char* seed, void* digest, const void* src, unsigned size)
{
	switch (kind) {
	case HASH_MURMUR3 :
		MurmurHash3_x86_128(src, size, seed, digest);
		break;
	case HASH_SPOOKY2 :
		SpookyHash128(src, size, seed, digest);
		break;
	default:
		fprintf(stderr, "Internal inconsistency in hash function %u\n", kind);
		exit(EXIT_FAILURE);
		break;
	}
}

/****************************************************************************/
/* random */

void randomize(void* void_ptr, unsigned size)
{
	unsigned char* ptr = void_ptr;
	unsigned i;

	srand(time(0));

	for(i=0;i<size;++i)
		ptr[i] = rand();
}

/****************************************************************************/
/* lock */

#if HAVE_LOCKFILE
int lock_lock(const char* file)
{
	int f;

	f = open(file, O_CREAT | O_TRUNC | O_WRONLY, 0600);
	if (f == -1) {
		return -1;
	}

	/* exclusive lock, not blocking */
	if (flock(f, LOCK_EX | LOCK_NB) == -1) {
		close(f);
		return -1;
	}

	return 0;
}
#endif

#if HAVE_LOCKFILE
int lock_unlock(int f)
{
	/*
	 * Intentionally don't remove the lock file.
	 * Removing it just introduces race course with other process
	 * that could have already opened it.
	 */

	if (close(f) == -1) {
		return -1;
	}

	return 0;
}
#endif

