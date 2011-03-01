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

/****************************************************************************/
/* string */

static inline char* strtoken(char* s)
{
	while (*s && *s != ' ')
		++s;
	if (*s)
		*s++ = 0;
	while (*s == ' ')
		++s;
	return s;
}

static inline char* strskip(char* s)
{
	while (*s == ' ')
		++s;
	return s;
}

static inline int strgets(char* s, unsigned size, FILE* f)
{
	char* i = s;
	char* send = s + size;
	int c;

	while (1) {
		c = fgetc_unlocked(f);

		if (c == EOF || c == '\n')
			break;

		*i++ = c;

		if (i == send) {
			return -1;
		}
	}

	if (c == EOF) {
		if (ferror_unlocked(f)) {
			return -1;
		}
		if (i == s)
			return 0;
	}

	/* remove ending spaces */
	while (i != s && isspace(i[-1]))
		--i;
	*i = 0;

	return 1;
}

static inline int stru(const char* s, unsigned* value)
{
	unsigned v;
	
	if (!*s)
		return -1;

	v = 0;
	while (*s>='0' && *s<='9') {
		v *= 10;
		v += *s - '0';
		++s;
	}

	if (*s)
		return -1;

	*value = v;

	return 0;
}

static inline int stru64(const char* s, uint64_t* value)
{
	uint64_t v;

	if (!*s)
		return -1;

	v = 0;
	while (*s>='0' && *s<='9') {
		v *= 10;
		v += *s - '0';
		++s;
	}

	if (*s)
		return -1;

	*value = v;

	return 0;
}

static char strhexset[16] = "0123456789abcdef";

static inline void strenchex(char* str, const void* void_data, unsigned data_len)
{
	const unsigned char* data = void_data;
	unsigned i;

	for(i=0;i<data_len;++i) {
		unsigned char b = data[i];
		*str++ = strhexset[b >> 4];
		*str++ = strhexset[b & 0xF];
	}
}

static inline char* strdechex(void* void_data, unsigned data_len, char* str)
{
	unsigned char* data = void_data;
	unsigned i;

	for(i=0;i<data_len;++i) {
		char c0;
		char c1;
		unsigned char b0;
		unsigned char b1;

		c0 = *str;
		if (c0 >= 'A' && c0 <= 'F')
			b0 = c0 - 'A' + 10;
		else if (c0 >= 'a' && c0 <= 'f')
			b0 = c0 - 'a' + 10;
		else if (c0 >= '0' && c0 <= '9')
			b0 = c0 - '0';
		else
			return str;
		++str;

		c1 = *str;
		if (c1 >= 'A' && c1 <= 'F')
			b1 = c1 - 'A' + 10;
		else if (c1 >= 'a' && c1 <= 'f')
			b1 = c1 - 'a' + 10;
		else if (c1 >= '0' && c1 <= '9')
			b1 = c1 - '0';
		else
			return str;
		++str;

		data[i] = (b0 << 4) | b1;
	}

	return 0;
}

