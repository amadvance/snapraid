// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Andrea Mazzoleni

#include "os/portable.h"

#include "str.h"
#include "support.h"

/****************************************************************************/
/* string */

char* strpolish(char* s)
{
	char* i = s;

	while (*i) {
		if (isspace((unsigned char)*i) || !isprint((unsigned char)*i))
			*i = ' ';
		++i;
	}

	return s;
}

unsigned strsplit(char** split_map, unsigned split_max, char* str, const char* delimiters)
{
	unsigned mac = 0;

	/* skip initial delimiters */
	str += strspn(str, delimiters);

	while (*str != 0 && mac < split_max) {
		/* start of the token */
		split_map[mac] = str;
		++mac;

		/* find the first delimiter or the end of the string */
		str += strcspn(str, delimiters);

		/* put the final terminator if missing */
		if (*str != 0)
			*str++ = 0;

		/* skip trailing delimiters */
		str += strspn(str, delimiters);
	}

	return mac;
}

unsigned argsplit(const char** split_map, unsigned split_max, char* str)
{
	unsigned mac = 0;

	/* skip leading whitespace */
	while (*str != 0 && isspace((unsigned char)*str))
		++str;

	while (*str != 0 && mac < split_max) {
		char* dst = str;
		split_map[mac++] = dst;

#ifdef _WIN32
		/*
		 * cmd.exe rules:
		 *   Unquoted: '^' escapes next char; '"' opens a quoted block.
		 *   Quoted:   '"' closes the block; '^' is an ordinary character.
		 */
		int quoted = 0;

		while (*str != 0 && (quoted || !isspace((unsigned char)*str))) {
			if (quoted) {
				if (*str == '"') {
					quoted = 0;
					++str;
				} else {
					*dst++ = *str++;
				}
			} else {
				if (*str == '^' && *(str + 1) != 0) {
					++str;
					*dst++ = *str++;
				} else if (*str == '"') {
					quoted = 1;
					++str;
				} else {
					*dst++ = *str++;
				}
			}
		}

#else
		/*
		 * bash rules:
		 *   Single-quoted block: everything literal, no exceptions.
		 *   Double-quoted block: '\' only escapes $, `, ", '\', newline;
		 *                        other backslashes are copied verbatim.
		 *   Unquoted '\': always escapes the immediately following char.
		 */

		/* set of characters that '\' can escape inside double quotes */
		static const char dq_escapable[] = "$`\"\\\n";

		while (*str != 0 && !isspace((unsigned char)*str)) {

			if (*str == '\'') {
				/* single-quoted block: truly everything is literal */
				++str;
				while (*str != 0 && *str != '\'')
					*dst++ = *str++;
				if (*str == '\'')
					++str;

			} else if (*str == '"') {
				/* double-quoted block */
				++str;
				while (*str != 0 && *str != '"') {
					if (*str == '\\' && strchr(dq_escapable, *(str + 1))) {
						++str;
					}
					*dst++ = *str++;
				}
				if (*str == '"')
					++str;

			} else if (*str == '\\') {
				/* unquoted backslash: next character always literal */
				++str;
				if (*str != 0)
					*dst++ = *str++;

			} else {
				*dst++ = *str++;
			}
		}
#endif

		if (*str != 0)
			++str; /* step past the delimiter */

		/* skip whitespace before the next token */
		while (*str != 0 && isspace((unsigned char)*str))
			++str;

		/* null-terminate after str has already moved on */
		*dst = 0;
	}

	return mac;
}

char* strtrim(char* str)
{
	char* begin;
	char* end;

	begin = str;
	while (begin[0] && isspace((unsigned char)begin[0]))
		++begin;

	end = begin + strlen(begin);
	while (end > begin && isspace((unsigned char)end[-1]))
		--end;

	end[0] = 0;

	if (begin != str)
		memmove(str, begin, end - begin + 1);

	return str;
}

char* strlwr(char* str)
{
	char* s = str;

	while (*s) {
		*s = tolower((unsigned char)*s);
		++s;
	}

	return str;
}

char* worddigitstr(const char* haystack, const char* needle)
{
	size_t len = strlen(needle);
	const char* s;

	if (len == 0)
		return NULL;

	for (s = haystack; (s = strstr(s, needle)) != NULL; ++s) {

		/* left boundary */
		if (s == haystack || isspace((unsigned char)s[-1]) || isdigit((unsigned char)s[-1])) {
			/* right boundary */
			if (s[len] == 0 || isspace((unsigned char)s[len]) || isdigit((unsigned char)s[len])) {
				return (char*)s;
			}
		}
	}

	return NULL;
}

int strtoi(const char* nptr, char** endptr, int base)
{
	long val;
	int save_errno = errno;

	errno = 0;
	val = strtol(nptr, endptr, base);

	if (errno == ERANGE) {
		return val > 0 ? INT_MAX : INT_MIN;
	}

	if (val > INT_MAX) {
		errno = ERANGE;
		return INT_MAX;
	}

	if (val < INT_MIN) {
		errno = ERANGE;
		return INT_MIN;
	}

	if (errno == 0) {
		errno = save_errno;
	}

	return (int)val;
}

unsigned strtou(const char* nptr, char** endptr, int base)
{
	const char* p = nptr;
	char* end;
	unsigned long val;
	int neg = 0;
	int save_errno = errno;

	while (isspace((unsigned char)*p)) {
		++p;
	}

	if (*p == '-') {
		neg = 1;
		++p;
	} else if (*p == '+') {
		++p;
	}

	errno = 0;
	val = strtoul(p, &end, base);

	if (end == p) {
		/* no conversion was performed */
		if (endptr) {
			*endptr = (char*)nptr;
		}
		errno = save_errno;
		return 0;
	}

	if (endptr) {
		*endptr = end;
	}

	if (errno == ERANGE || val > UINT_MAX) {
		errno = ERANGE;
		return UINT_MAX;
	}

	if (errno == 0) {
		errno = save_errno;
	}

	if (neg) {
		return (unsigned)-val;
	}

	return (unsigned)val;
}

/****************************************************************************/
/* wnmatch */

/**
 * Helper function for case-insensitive character comparison
 */
static inline int char_match(char p, char t)
{
#ifdef WIN32
	return tolower((unsigned char)p) == tolower((unsigned char)t);
#else
	return p == t;
#endif
}

/**
 * Match character class [...]
 * Return 0 if NOT matched
 */
static const char* match_class(const char* p, char t)
{
	int negate = 0;
	int matched = 0;

	if (*p == '!' || *p == '^') {
		negate = 1;
		++p;
	}

	while (*p && *p != ']') {
		if (p[1] == '-' && p[2] != ']' && p[2] != 0) {
			/* range [a-z] */
			char start = *p;
			char end = p[2];
#ifdef WIN32
			start = tolower((unsigned char)start);
			end = tolower((unsigned char)end);
			t = tolower((unsigned char)t);
#endif
			if (t >= start && t <= end)
				matched = 1;
			p += 3;
		} else {
			/* single character */
			if (char_match(*p, t))
				matched = 1;
			++p;
		}
	}

	if (*p == ']')
		++p;

	if (negate)
		matched = !matched;
	if (!matched)
		return 0;

	return p;
}

int wnmatch_sub(const char* p, const char* t, int match_sub)
{
	char p1 = 0; /* previous char */
	while (*p) {
		char p0 = *p;
		switch (p0) {
		case '?' :
			/* ? matches any single character except / */
			if (*t == 0 || *t == '/')
				return 1;
			++p;
			++t;
			break;
		case '*' :
			/* check for ** */
			if (p[1] == '*') {
				/* skip the ** */
				p += 2;

				/* munge all * */
				while (*p == '*')
					++p;

				/* if its not near a slash, it's like a single * */
				if (p1 == '/' || *p == '/') {
					/* a ** at end matches everything */
					if (*p == 0)
						return 0;

					/*
					 * In between slashes matches to nothing
					 *
					 * Check for /##/ or ^##/ (^ start of string)
					 *
					 * This is required for:
					 * "/##/file.txt" matching "/file.txt"
					 * "##/file.txt" matching "file.txt"
					 * "x##/file.txt" NOT matching "xfile.txt"
					 */
					if (*p == '/' && (p1 == 0 || p1 == '/')) {
						/* try reducing to nothing */
						if (wnmatch_sub(p + 1, t, match_sub) == 0)
							return 0;
						/* otherwise / should match in the text */
					}

					/* try matching with 0 or more characters */
					while (*t) {
						if (wnmatch_sub(p, t, match_sub) == 0)
							return 0;
						++t;
					}

					/* try matching at the end */
					return wnmatch_sub(p, t, match_sub);
				}
			} else {
				/* skip the * */
				++p;
			}

			/* a * at end matches rest of segment */
			if (*p == 0) {
				while (*t && *t != '/')
					++t;
				return *t != 0;
			}

			/* try matching with 0 or more characters */
			while (*t && *t != '/') {
				if (wnmatch_sub(p, t, match_sub) == 0)
					return 0;
				++t;
			}

			/* try matching at the end */
			return wnmatch_sub(p, t, match_sub);
		case '[' :
			/* character class */
			if (*t == 0 || *t == '/')
				return 1;

			p = match_class(p + 1, *t);
			if (!p)
				return 1;

			++t;
			break;
		default :
			/* literal character */
			if (*t == 0 || !char_match(*p, *t))
				return 1;
			++p;
			++t;
			break;
		}
		p1 = p0;
	}

	/* if we match sub directory */
	if (match_sub) {
		/* match successfully only if we are at a directory border */
		return *t != '/';
	} else {
		/* match successfully if we've consumed all text */
		return *t != 0;
	}
}

