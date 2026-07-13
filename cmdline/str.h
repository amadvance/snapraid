// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Andrea Mazzoleni

#ifndef __STR_H
#define __STR_H

#include "app.h"
#include "str.h"

/****************************************************************************/
/* string */

/**
 * Polish a string.
 *
 * Not printable chars are replaced by spaces.
 *
 * Note that the passed string is modified.
 */
char* strpolish(char* s);

/**
 * Split a string into tokens in-place.
 *
 * Scans @str for tokens separated by any character in @delimiters,
 * null-terminates each token by overwriting its trailing delimiter,
 * and stores a pointer to each token's start in @split_map.
 *
 * Leading and consecutive delimiters are silently skipped, so the
 * output never contains empty tokens.
 *
 * The input string is modified in-place; @split_map entries point
 * directly into @str and are only valid for its lifetime.
 *
 * \param split_map Output array receiving pointers to each token.
 * \param split_max Capacity of @split_map; at most this many tokens are stored.
 * \param str Input string to tokenise (modified in-place).
 * \param delimiters Null-terminated set of delimiter characters.
 * \return Number of tokens stored in @split_map.
 *
 * Example:
 *   char  buf[] = "  one,two,,three  ";
 *   char* map[8];
 *   unsigned n = strsplit(map, 8, buf, " ,");
 *   // n == 3, map[] == { "one", "two", "three" }
 */
unsigned strsplit(char** split_map, unsigned split_max, char* str, const char* delimiters);

/**
 * Split a command-line string into an argument vector,
 * applying the quoting rules of the platform's native shell.
 *
 * The input string is modified in-place: quotes and escape characters
 * are stripped and each token is null-terminated. Whitespace is the
 * implicit token delimiter on both platforms.
 *
 * bash quoting (Linux / macOS):
 *   'single quotes'  - Everything literal; no escapes processed inside.
 *   "double quotes"  - '\' only escapes $, `, ", '\', and newline;
 *                      all other backslashes are kept as-is.
 *   \x (unquoted)    - Next character taken literally (even whitespace).
 *
 * cmd.exe quoting (Windows):
 *   "double quotes"  - Toggle quoted mode; inside, '^' is NOT special.
 *   ^x (unquoted)    - Next character taken literally.
 *   Single quotes and '\' carry no special meaning in either mode.
 *
 * \param split_map Output array receiving pointers to each argument.
 * \param split_max Capacity of @split_map; at most this many tokens are stored.
 * \param str Input command-line string (modified in-place).
 * \return Number of arguments stored in @split_map.
 *
 * Example (bash):
 *   char  buf[] = "one 'two three' \"say \\\"hi\\\"\" back\\slash";
 *   char* map[8];
 *   unsigned n = argsplit(map, 8, buf);
 *   // n==4, map[] == { "one", "two three", "say \"hi\"", "backslash" }
 *
 * Example (cmd.exe):
 *   char  buf[] = "one \"two three\" ^&safe ^\"quoted^\"";
 *   char* map[8];
 *   unsigned n = argsplit(map, 8, buf);
 *   // n==4, map[] == { "one", "two three", "&safe", "\"quoted\"" }
 */
unsigned argsplit(const char** split_map, unsigned split_max, char* str);

/**
 * Trim spaces from the start and the end
 */
char* strtrim(char* s);

/**
 * Lower case
 */
char* strlwr(char* s);

/*
 * Find the first occurrence of 'needle' in 'haystack' only if it
 * appears as a separate word (space, number or string boundaries).
 *
 * Returns a pointer to the beginning of the match, or NULL if not found.
 */
char* worddigitstr(const char* haystack, const char* needle);

/**
 * Convert a string to an integer, with the same semantics as strtol.
 */
int strtoi(const char* nptr, char** endptr, int base);

/**
 * Convert a string to an unsigned integer, with the same semantics as strtoul.
 */
unsigned strtou(const char* nptr, char** endptr, int base);

/****************************************************************************/
/* match */

/*
 * Wild match function.
 *
 * If match_sub is !=0, it matches sub directory. Specifically it matches if the
 * string "t" is not fully consumed and the next character to consume is a /.
 *
 * - ? matches any single character except /
 * - * matches any sequence of characters except /
 * - ** (nearby a /) matches everything including /
 * - ** (not near a /) like *
 * - ##/ reduces to nothing in addition to the normal matching of ** (using # instead of * to not mess the C comment)
 * - [...] matches character classes with support for ranges and negation lile [!...] or [^...], except /
 *
 * \return 0 if it matches
 */
int wnmatch_sub(const char* p, const char* t, int match_sub);

static inline int wnmatch(const char* p, const char* t)
{
	return wnmatch_sub(p, t, 0);
}

#endif

