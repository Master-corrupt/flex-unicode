/*
 * unicode.h - interface of the Unicode-escape expansion for flex (Project 3).
 *
 * flex is byte-oriented: its character classes are sets of 8-bit bytes and
 * its regular expressions operate on bytes.  This module rewrites the
 * `\uXXXX` / `\u{...}` escapes (which standard flex does not understand) into
 * equivalent byte-level flex patterns:
 *
 *   - a single code point  \uXXXX      becomes its UTF-8 byte sequence
 *     written as `(\xHH\xHH...)`;
 *   - a code point range   [\uXXXX-\uYYYY]  inside a character class becomes a
 *     byte-level alternation that matches the UTF-8 encodings of every code
 *     point in the range, e.g.
 *       [\u4e00-\u9fff]  ->  (\xE4[\xB8-\xBF][\x80-\xBF]|
 *                             [\xE5-\xE8][\x80-\xBF][\x80-\xBF]|
 *                             \xE9[\x80-\xBF][\x80-\xBF])
 *
 * The expansion only rewrites patterns/definition values; section 3 user code,
 * `%{...%}` blocks, comments and actions are copied verbatim.
 */

#ifndef FLEX_UNICODE_H
#define FLEX_UNICODE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Return a malloc'd, NUL-terminated string that is the Unicode-expanded
 * version of `input` (which may be freed by the caller). */
char *unicode_expand(const char *input);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_UNICODE_H */
