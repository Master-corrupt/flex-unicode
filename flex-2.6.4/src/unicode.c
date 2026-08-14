/*
 * unicode.c - Unicode-escape expansion for flex (Project 3).
 *
 * See unicode.h for an overview.  The entry point is unicode_expand().
 *
 * The input .l file is processed with a small state machine that tracks:
 *   - the three flex sections (separated by `%%` lines),
 *   - whether we are inside a `%{ ... %}` code block,
 *   - whether we are inside a comment or a quoted string,
 *   - in section 2, whether a line begins a new rule (pattern) or is a
 *     continuation of the previous action (indented).
 *
 * Only the *pattern* part of a rule and the *value* part of a definition are
 * rewritten; everything else is emitted verbatim.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include "unicode.h"

/* ------------------------------------------------------------------ */
/* tiny growable string buffer                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    char  *p;
    size_t len;
    size_t cap;
} strbuf;

static void sb_init(strbuf *b)
{
    b->p = NULL;
    b->len = 0;
    b->cap = 0;
}

static void sb_reserve(strbuf *b, size_t extra)
{
    if (b->len + extra + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 256;
        while (nc < b->len + extra + 1) nc *= 2;
        char *np = (char *)realloc(b->p, nc);
        if (!np) {
            fprintf(stderr, "flex-unicode: out of memory\n");
            exit(1);
        }
        b->p = np;
        b->cap = nc;
    }
}

static void sb_appendn(strbuf *b, const char *s, size_t n)
{
    sb_reserve(b, n);
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

static void sb_append(strbuf *b, const char *s)
{
    sb_appendn(b, s, strlen(s));
}

static void sb_appendc(strbuf *b, char c)
{
    sb_appendn(b, &c, 1);
}

static void sb_appendf(strbuf *b, const char *fmt, ...)
{
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    sb_append(b, tmp);
}

static char *sb_detach(strbuf *b)
{
    char *s = b->p ? b->p : strdup("");
    b->p = NULL;
    b->len = b->cap = 0;
    return s;
}

/* ------------------------------------------------------------------ */
/* UTF-8 encoding and byte-range regex emission                       */
/* ------------------------------------------------------------------ */

static int utf8_len(unsigned int cp)
{
    if (cp < 0x80) return 1;
    if (cp < 0x800) return 2;
    if (cp < 0x10000) return 3;
    return 4;
}

static void encode_utf8(unsigned int cp, unsigned char out[4], int *len)
{
    int n = utf8_len(cp);
    int i;
    if (n == 1) {
        out[0] = (unsigned char)cp;
    } else if (n == 2) {
        out[0] = (unsigned char)(0xC0 | (cp >> 6));
        out[1] = (unsigned char)(0x80 | (cp & 0x3F));
    } else if (n == 3) {
        out[0] = (unsigned char)(0xE0 | (cp >> 12));
        out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (unsigned char)(0x80 | (cp & 0x3F));
    } else {
        out[0] = (unsigned char)(0xF0 | (cp >> 18));
        out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (unsigned char)(0x80 | (cp & 0x3F));
    }
    *len = n;
}

/* smallest code point whose UTF-8 encoding has `len` bytes */
static unsigned int min_cp_for_len(int len)
{
    switch (len) {
    case 1: return 0x00;
    case 2: return 0x80;
    case 3: return 0x800;
    case 4: return 0x10000;
    default: return 0;
    }
}

/* largest code point whose UTF-8 encoding has `len` bytes */
static unsigned int max_cp_for_len(int len)
{
    switch (len) {
    case 1: return 0x7F;
    case 2: return 0x7FF;
    case 3: return 0xFFFF;
    case 4: return 0x10FFFF;
    default: return 0;
    }
}

static void emit_byte(strbuf *b, unsigned int x)
{
    sb_appendf(b, "\\x%02X", x & 0xFF);
}

static void emit_byte_class(strbuf *b, unsigned int lo, unsigned int hi)
{
    if (lo == hi)
        emit_byte(b, lo);
    else {
        sb_appendf(b, "[\\x%02X-\\x%02X]", lo & 0xFF, hi & 0xFF);
    }
}

/* Emit a regex fragment matching the UTF-8 encodings of code points in
 * the inclusive range [lo, hi].  The fragment is a sequence of alternatives
 * joined with '|'.  *first tracks whether we are about to emit the first
 * alternative (no leading '|' for it). */
static void emit_cp_range(strbuf *b, unsigned int lo, unsigned int hi, int *first)
{
    unsigned char a[4], bb[4];
    int na, nb, k, j;

    if (lo > hi) return;

    encode_utf8(lo, a, &na);
    encode_utf8(hi, bb, &nb);

    if (na != nb) {
        /* split by byte length */
        emit_cp_range(b, lo, max_cp_for_len(na), first);
        for (j = na + 1; j < nb; ++j)
            emit_cp_range(b, min_cp_for_len(j), max_cp_for_len(j), first);
        emit_cp_range(b, min_cp_for_len(nb), hi, first);
        return;
    }

    if (*first)
        *first = 0;
    else
        sb_appendc(b, '|');

    /* same length: strip the common prefix */
    k = 0;
    while (k < na && a[k] == bb[k]) k++;
    for (j = 0; j < k; ++j)
        emit_byte(b, a[j]);

    if (k == na)
        return;   /* a single code point */

    /* head: a[k] with the tail running from a[k+1..] up to the max */
    emit_byte(b, a[k]);
    for (j = k + 1; j < na; ++j)
        emit_byte_class(b, a[j], 0xBF);

    /* middle: bytes (a[k], b[k]) with a full tail */
    if (a[k] + 1 <= bb[k] - 1) {
        sb_appendc(b, '|');
        emit_byte_class(b, a[k] + 1, bb[k] - 1);
        for (j = k + 1; j < na; ++j)
            emit_byte_class(b, 0x80, 0xBF);
    }

    /* tail: b[k] with the tail running from the min up to b[k+1..] */
    sb_appendc(b, '|');
    emit_byte(b, bb[k]);
    for (j = k + 1; j < na; ++j)
        emit_byte_class(b, 0x80, bb[j]);
}

/* ------------------------------------------------------------------ */
/* \u escape parsing                                                  */
/* ------------------------------------------------------------------ */

/* Parse a \uXXXX or \u{XXXXXX} escape at `s`.
 * Returns the code point and advances *consumed past the escape. */
static unsigned int parse_u_escape(const char *s, int *consumed)
{
    unsigned int cp = 0;
    int n = 0, i;

    if (s[0] != '\\' || s[1] != 'u')
        return 0;

    if (s[2] == '{') {
        i = 3;
        while (isxdigit((unsigned char)s[i]) && n < 6) {
            cp = cp * 16 + (unsigned)(isdigit((unsigned char)s[i])
                                      ? s[i] - '0'
                                      : (tolower((unsigned char)s[i]) - 'a' + 10));
            ++n;
            ++i;
        }
        if (s[i] == '}') ++i;
        *consumed = i;
    } else {
        for (i = 2; i < 6 && isxdigit((unsigned char)s[i]); ++i) {
            cp = cp * 16 + (unsigned)(isdigit((unsigned char)s[i])
                                      ? s[i] - '0'
                                      : (tolower((unsigned char)s[i]) - 'a' + 10));
            ++n;
        }
        *consumed = i;
    }

    /* surrogates are not valid scalar values; substitute U+FFFD */
    if (cp >= 0xD800 && cp <= 0xDFFF)
        cp = 0xFFFD;
    if (cp > 0x10FFFF)
        cp = 0xFFFD;

    return cp;
}

/* Emit a single code point as `(\xHH\xHH...)`. */
static void emit_cp(strbuf *b, unsigned int cp)
{
    unsigned char buf[4];
    int n, i;
    encode_utf8(cp, buf, &n);
    sb_appendc(b, '(');
    for (i = 0; i < n; ++i)
        emit_byte(b, buf[i]);
    sb_appendc(b, ')');
}

/* ------------------------------------------------------------------ */
/* character class expansion                                          */
/* ------------------------------------------------------------------ */

/* Find the index just past the closing ']' of a character class that starts
 * at `content` (the '[').  Returns -1 if unterminated. */
static int find_class_end(const char *content, size_t len)
{
    size_t i;
    for (i = 1; i < len; ++i) {
        if (content[i] == '\\' && i + 1 < len) {
            ++i;               /* skip escaped char */
            continue;
        }
        if (content[i] == ']')
            return (int)i + 1;
    }
    return -1;
}

/* Rewrite one `[...]` class (content points at '[').  If the class contains
 * no `\u` escapes, it is copied verbatim.  Otherwise it is expanded into a
 * byte-level alternation.  Returns the number of input characters consumed. */
static int expand_class(const char *content, size_t len, strbuf *out)
{
    int end = find_class_end(content, len);
    int neg = 0;
    strbuf bytes;       /* non-unicode part (verbatim) */
    strbuf uni;         /* unicode alternations */
    int has_uni = 0;
    size_t i;

    if (end < 0) {
        /* unterminated class: copy verbatim */
        sb_appendn(out, content, len);
        return (int)len;
    }

    sb_init(&bytes);
    sb_init(&uni);

    i = 1;                              /* past '[' */
    if (i < (size_t)end && content[i] == '^') {
        neg = 1;
        ++i;
    }

    while (i < (size_t)(end - 1)) {     /* end-1 is the closing ']' */
        if (content[i] == '\\' && content[i + 1] == 'u') {
            int consumed = 0;
            unsigned int cp = parse_u_escape(content + i, &consumed);
            unsigned int cp2 = 0;
            int consumed2 = 0;
            int j = i + consumed;
            /* range \uXXXX-\uYYYY ? */
            if (j + 1 < (size_t)(end - 1) && content[j] == '-' &&
                content[j + 1] == '\\' && content[j + 2] == 'u') {
                cp2 = parse_u_escape(content + j + 1, &consumed2);
                if (has_uni) sb_appendc(&uni, '|');
                sb_appendc(&uni, '(');
                { int first = 1; emit_cp_range(&uni, cp, cp2, &first); }
                sb_appendc(&uni, ')');
                i = j + 1 + consumed2;
            } else {
                if (has_uni) sb_appendc(&uni, '|');
                emit_cp(&uni, cp);
                i += consumed;
            }
            has_uni = 1;
        } else {
            /* copy one byte-level item verbatim (char or escape) */
            if (content[i] == '\\' && i + 1 < (size_t)(end - 1)) {
                sb_appendn(&bytes, content + i, 2);
                i += 2;
                /* a \x or octal escape may have more digits */
                if (content[i - 1] == 'x') {
                    while (i < (size_t)(end - 1) && isxdigit((unsigned char)content[i])) {
                        sb_appendc(&bytes, content[i]);
                        ++i;
                    }
                } else if (isdigit((unsigned char)content[i - 1])) {
                    while (i < (size_t)(end - 1) && isdigit((unsigned char)content[i])) {
                        sb_appendc(&bytes, content[i]);
                        ++i;
                    }
                }
            } else {
                sb_appendc(&bytes, content[i]);
                ++i;
            }
        }
    }

    if (!has_uni) {
        /* no unicode: emit the class unchanged */
        sb_appendn(out, content, (size_t)end);
        free(bytes.p);
        free(uni.p);
        return end;
    }

    if (neg) {
        fprintf(stderr,
                "flex-unicode: warning: negation [^...] of a Unicode class is not "
                "supported; the class is left unchanged\n");
        sb_appendn(out, content, (size_t)end);
        free(bytes.p);
        free(uni.p);
        return end;
    }

    /* build the union: ([bytes]|uni1|uni2|...) */
    sb_appendc(out, '(');
    if (bytes.len > 0) {
        sb_appendc(out, '[');
        sb_appendn(out, bytes.p, bytes.len);
        sb_appendc(out, ']');
        if (uni.len > 0) sb_appendc(out, '|');
    }
    if (uni.len > 0)
        sb_appendn(out, uni.p, uni.len);
    sb_appendc(out, ')');

    free(bytes.p);
    free(uni.p);
    return end;
}

/* ------------------------------------------------------------------ */
/* pattern fragment expansion                                         */
/* ------------------------------------------------------------------ */

/* Rewrite `\uXXXX` escapes and `[...]` classes within one pattern (or a
 * definition value).  Quotes are respected so that a `\u` inside a quoted
 * string is still expanded (it is part of the pattern). */
static void expand_pattern(const char *s, size_t len, strbuf *out)
{
    size_t i = 0;
    while (i < len) {
        if (s[i] == '\\' && i + 1 < len && s[i + 1] == 'u') {
            int consumed = 0;
            unsigned int cp = parse_u_escape(s + i, &consumed);
            emit_cp(out, cp);
            i += consumed;
        } else if (s[i] == '[') {
            int n = expand_class(s + i, len - i, out);
            i += (size_t)n;
        } else {
            sb_appendc(out, s[i]);
            ++i;
        }
    }
}

/* ------------------------------------------------------------------ */
/* top-level expansion                                                */
/* ------------------------------------------------------------------ */

/* Is the line `s` (length `len`, without newline) a `%%` section marker? */
static int is_section_marker(const char *s, size_t len)
{
    size_t i;
    if (len < 2 || s[0] != '%' || s[1] != '%') return 0;
    for (i = 2; i < len; ++i)
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r')
            return 0;
    return 1;
}

/* Does the line begin with whitespace (i.e. is it an action continuation)? */
static int begins_with_ws(const char *s, size_t len)
{
    return len > 0 && (s[0] == ' ' || s[0] == '\t');
}

/* Scan a section-2 rule line `s` and expand the pattern part (everything up
 * to the first unquoted whitespace outside "..." and [...]). */
static void expand_rule_line(const char *s, size_t len, strbuf *out)
{
    size_t i = 0;
    while (i < len) {
        if (s[i] == '"') {
            /* quoted string: copy verbatim (a \u inside a flex string is
             * still a pattern escape and must be expanded too) */
            sb_appendc(out, s[i]);
            ++i;
            while (i < len && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < len && s[i + 1] == 'u') {
                    int consumed = 0;
                    unsigned int cp = parse_u_escape(s + i, &consumed);
                    emit_cp(out, cp);
                    i += consumed;
                } else {
                    sb_appendc(out, s[i]);
                    ++i;
                }
            }
            if (i < len) { sb_appendc(out, s[i]); ++i; }
        } else if (s[i] == '[') {
            int n = expand_class(s + i, len - i, out);
            i += (size_t)n;
        } else if (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') {
            /* end of pattern: copy the remainder (the action) verbatim */
            sb_appendn(out, s + i, len - i);
            return;
        } else if (s[i] == '\\' && i + 1 < len && s[i + 1] == 'u') {
            int consumed = 0;
            unsigned int cp = parse_u_escape(s + i, &consumed);
            emit_cp(out, cp);
            i += consumed;
        } else {
            sb_appendc(out, s[i]);
            ++i;
        }
    }
}

/* Expand a section-1 definition line: `NAME  value` (value may contain
 * Unicode escapes).  Non-definition lines are copied verbatim. */
static void expand_def_line(const char *s, size_t len, strbuf *out)
{
    size_t i = 0;

    /* A definition starts with an identifier at column 0. */
    if (len == 0 || !(isalpha((unsigned char)s[0]) || s[0] == '_')) {
        sb_appendn(out, s, len);
        return;
    }
    while (i < len && (isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '-'))
        ++i;
    /* skip whitespace between NAME and value */
    while (i < len && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    /* copy NAME + whitespace, then expand the value */
    sb_appendn(out, s, i);
    expand_pattern(s + i, len - i, out);
}

char *unicode_expand(const char *input)
{
    strbuf out;
    const char *p = input;
    const char *line_start = input;
    int section = 1;
    int in_code_block = 0;

    sb_init(&out);

    for (;;) {
        const char *eol;

        if (*p == '\0') {
            /* flush the last line (may lack a newline) */
            eol = p;
            /* process below */
        } else {
            eol = p;
            while (*eol != '\0' && *eol != '\n') ++eol;
        }

        {
            size_t len = (size_t)(eol - line_start);
            const char *s = line_start;

            if (section == 3) {
                sb_appendn(&out, s, len);
            } else if (is_section_marker(s, len)) {
                sb_appendn(&out, s, len);
                section = (section == 1) ? 2 : 3;
            } else if (section == 1) {
                /* %{ ... %} code blocks and option/start-condition lines */
                if (!in_code_block && len > 0 && s[0] == '%' && s[1] == '{') {
                    in_code_block = 1;
                    sb_appendn(&out, s, len);
                } else if (in_code_block && len > 0 && s[0] == '%' && s[1] == '}') {
                    in_code_block = 0;
                    sb_appendn(&out, s, len);
                } else if (in_code_block || (len > 0 && s[0] == '%')) {
                    sb_appendn(&out, s, len);
                } else {
                    expand_def_line(s, len, &out);
                }
            } else { /* section == 2 */
                if (begins_with_ws(s, len) || len == 0 ||
                    (len > 0 && (s[0] == '%' || s[0] == '}'))) {
                    /* action continuation */
                    sb_appendn(&out, s, len);
                } else {
                    expand_rule_line(s, len, &out);
                }
            }
        }

        if (*eol == '\0') break;
        sb_appendc(&out, '\n');
        line_start = eol + 1;
        p = eol + 1;
    }

    return sb_detach(&out);
}
