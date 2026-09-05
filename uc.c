#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "uc.h"

enum
{
    UTF8_STARTED = 0,
    UTF8_DONE = 1,

    UTF8_BAD_START = -1,
    UTF8_BAD_CONT = -2,
    UTF8_BAD_SEQ = -3
};

struct uc_ctx
{
    struct uc_opts opts;

    /* UTF-8 decoder state */
    int need;
    uint32_t cp;
    uint32_t lower;
    uint32_t upper;

    unsigned long issues;
};

static const char red[] = "\x1b[0;31m";
static const char reset[] = "\x1b[0m";

struct uc_ctx *uc_new(const struct uc_opts *opts)
{
    struct uc_ctx *c;

    if (!opts)
        return NULL;

    if (opts->mode != UC_MODE_HIGHLIGHT &&
        opts->mode != UC_MODE_SHOW &&
        opts->mode != UC_MODE_CLEAR)
        return NULL;

    if (opts->enc != UC_ENC_ASCII &&
        opts->enc != UC_ENC_CP1252 &&
        opts->enc != UC_ENC_CP1251)
        return NULL;

    c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;

    c->opts = *opts;
    return c;
}

void uc_free(struct uc_ctx *ctx)
{
    free(ctx);
}

unsigned long uc_issues(const struct uc_ctx *ctx)
{
    return ctx ? ctx->issues : 0;
}

/*
 * Output helpers.
 *
 * Write callback contract:
 *   return 0 on success
 *   return negative value on error
 */

static int emit(uc_write_fn out, void *arg,
                const char *buf, size_t len)
{
    if (len == 0)
        return 0;

    return out(buf, len, arg) == 0 ? 0 : -1;
}

static int emits(uc_write_fn out, void *arg,
                 const char *s)
{
    return emit(out, arg, s, strlen(s));
}

static int emit_color(const struct uc_ctx *c, uc_write_fn out, void *arg,
                      const char *buf, size_t len)
{
    if (len == 0)
        return 0;

    if (!c->opts.use_color)
        return emit(out, arg, buf, len);

    if (emits(out, arg, red) < 0)
        return -1;

    if (emit(out, arg, buf, len) < 0)
        return -1;

    return emits(out, arg, reset);
}

static int emits_color(const struct uc_ctx *c, uc_write_fn out, void *arg,
                       const char *s)
{
    return emit_color(c, out, arg, s, strlen(s));
}

/*
 * UTF-8 encoder.
 */

static int utf8_encode(uint32_t cp, unsigned char out[4])
{
    if (cp > 0x10FFFF)
        return 0;

    if (cp >= 0xD800 && cp <= 0xDFFF)
        return 0;

    if (cp < 0x80)
    {
        out[0] = (unsigned char)cp;
        return 1;
    }

    if (cp < 0x800)
    {
        out[0] = (unsigned char)(0xC0 | (cp >> 6));
        out[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    }

    if (cp < 0x10000)
    {
        out[0] = (unsigned char)(0xE0 | (cp >> 12));
        out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }

    out[0] = (unsigned char)(0xF0 | (cp >> 18));
    out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (unsigned char)(0x80 | (cp & 0x3F));
    return 4;
}

static int emit_cp_plain(uc_write_fn out, void *arg,
                         uint32_t cp)
{
    unsigned char buf[4];
    int len;

    len = utf8_encode(cp, buf);
    if (len <= 0)
        return -1;

    return emit(out, arg, (const char *)buf, (size_t)len);
}

static int emit_cp_color(const struct uc_ctx *c, uc_write_fn out, void *arg,
                         uint32_t cp)
{
    unsigned char buf[4];
    int len;

    len = utf8_encode(cp, buf);
    if (len <= 0)
        return -1;

    return emit_color(c, out, arg, (const char *)buf, (size_t)len);
}

static int emit_show_cp(const struct uc_ctx *c, uc_write_fn out, void *arg,
                        uint32_t cp)
{
    char buf[16];
    int n;

    n = snprintf(buf, sizeof(buf), "[U+%04X]", (unsigned)cp);

    if (n < 0 || (size_t)n >= sizeof(buf))
        return -1;

    return emit_color(c, out, arg, buf, (size_t)n);
}

/*
 * Classification.
 */

static int is_preserved(uint32_t cp)
{
    return cp == 0x0A || cp == 0x0D || cp == 0x09;
}

static int is_ascii_printable(uint32_t cp)
{
    return cp >= 0x20 && cp <= 0x7E;
}

static int is_control(uint32_t cp)
{
    if (is_preserved(cp))
        return 0;

    if (cp <= 0x1F || cp == 0x7F)
        return 1;

    if (cp >= 0x80 && cp <= 0x9F)
        return 1;

    return 0;
}

static int is_unicode_space(uint32_t cp)
{
    if (cp == 0x00A0)
        return 1;

    if (cp == 0x1680)
        return 1;

    if (cp >= 0x2000 && cp <= 0x200A)
        return 1;

    if (cp == 0x202F || cp == 0x205F || cp == 0x3000)
        return 1;

    return 0;
}

static int is_zero_width_or_format(uint32_t cp)
{
    if (cp == 0x00AD)
        return 1;

    if (cp == 0x034F) /* COMBINING GRAPHEME JOINER */
        return 1;

    if (cp == 0x180E) /* MONGOLIAN VOWEL SEPARATOR */
        return 1;

    if (cp >= 0x200B && cp <= 0x200F)
        return 1;

    if (cp == 0x2028 || cp == 0x2029)
        return 1;

    if (cp >= 0x202A && cp <= 0x202E)
        return 1;

    if (cp >= 0x2060 && cp <= 0x2064)
        return 1;

    if (cp >= 0x2066 && cp <= 0x2069)
        return 1;

    if (cp >= 0x206A && cp <= 0x206F)
        return 1;

    if (cp == 0xFEFF)
        return 1;

    if (cp >= 0xFE00 && cp <= 0xFE0F)
        return 1;

    if (cp >= 0xFFF9 && cp <= 0xFFFB)
        return 1;

    if (cp == 0xE0001) /* LANGUAGE TAG */
        return 1;

    if (cp >= 0xE0020 && cp <= 0xE007F)
        return 1;

    if (cp >= 0xE0100 && cp <= 0xE01EF)
        return 1;

    return 0;
}

static int is_combining_mark(uint32_t cp)
{
    if (cp >= 0x0300 && cp <= 0x036F)
        return 1;

    if (cp >= 0x0483 && cp <= 0x0489)
        return 1;

    if (cp >= 0x1AB0 && cp <= 0x1AFF)
        return 1;

    if (cp >= 0x1DC0 && cp <= 0x1DFF)
        return 1;

    if (cp >= 0x20D0 && cp <= 0x20FF)
        return 1;

    if (cp >= 0xFE20 && cp <= 0xFE2F)
        return 1;

    return 0;
}

static int is_invisible(uint32_t cp)
{
    return is_unicode_space(cp) ||
           is_zero_width_or_format(cp) ||
           is_combining_mark(cp);
}

/*
 * Windows-1252 allowed Unicode code points.
 *
 * ASCII printable and preserved \n \r \t are handled separately.
 *
 * Note:
 * U+0080..U+009F are C1 controls in Unicode and are not allowed here.
 * Some bytes 0x80..0x9F in cp1252 map to separate Unicode characters,
 * which are listed explicitly below.
 */
static int is_allowed_cp1252(uint32_t cp)
{
    /*
     * Latin-1 Supplement printable range.
     *
     * This includes U+00A0 and U+00AD, but they will still be treated
     * as invisible/format characters by classification and clear policy.
     */
    if (cp >= 0x00A0 && cp <= 0x00FF)
        return 1;

    switch (cp)
    {
    case 0x0152: /* LATIN CAPITAL LIGATURE OE */
    case 0x0153: /* LATIN SMALL LIGATURE OE */
    case 0x0160: /* LATIN CAPITAL LETTER S WITH CARON */
    case 0x0161: /* LATIN SMALL LETTER S WITH CARON */
    case 0x0178: /* LATIN CAPITAL LETTER Y WITH DIAERESIS */
    case 0x017D: /* LATIN CAPITAL LETTER Z WITH CARON */
    case 0x017E: /* LATIN SMALL LETTER Z WITH CARON */
    case 0x0192: /* LATIN SMALL LETTER F WITH HOOK */
    case 0x02C6: /* MODIFIER LETTER CIRCUMFLEX ACCENT */
    case 0x02DC: /* SMALL TILDE */

    case 0x2013: /* EN DASH */
    case 0x2014: /* EM DASH */
    case 0x2018: /* LEFT SINGLE QUOTATION MARK */
    case 0x2019: /* RIGHT SINGLE QUOTATION MARK */
    case 0x201A: /* SINGLE LOW-9 QUOTATION MARK */
    case 0x201C: /* LEFT DOUBLE QUOTATION MARK */
    case 0x201D: /* RIGHT DOUBLE QUOTATION MARK */
    case 0x201E: /* DOUBLE LOW-9 QUOTATION MARK */
    case 0x2020: /* DAGGER */
    case 0x2021: /* DOUBLE DAGGER */
    case 0x2022: /* BULLET */
    case 0x2026: /* HORIZONTAL ELLIPSIS */
    case 0x2030: /* PER MILLE SIGN */
    case 0x2039: /* SINGLE LEFT-POINTING ANGLE QUOTATION MARK */
    case 0x203A: /* SINGLE RIGHT-POINTING ANGLE QUOTATION MARK */
    case 0x20AC: /* EURO SIGN */
    case 0x2122: /* TRADE MARK SIGN */
        return 1;

    default:
        return 0;
    }
}

/*
 * Windows-1251 allowed Unicode code points.
 *
 * ASCII printable and preserved \n \r \t are handled separately.
 */
static int is_allowed_cp1251(uint32_t cp)
{
    /*
     * Basic Russian letters:
     * U+0410..U+042F А-Я
     * U+0430..U+044F а-я
     */
    if (cp >= 0x0410 && cp <= 0x044F)
        return 1;

    switch (cp)
    {
    /* Symbols and punctuation from cp1251 */
    case 0x00A0: /* NO-BREAK SPACE */
    case 0x00A4: /* CURRENCY SIGN */
    case 0x00A6: /* BROKEN BAR */
    case 0x00A7: /* SECTION SIGN */
    case 0x00A9: /* COPYRIGHT SIGN */
    case 0x00AB: /* LEFT-POINTING DOUBLE ANGLE QUOTATION MARK */
    case 0x00AC: /* NOT SIGN */
    case 0x00AD: /* SOFT HYPHEN */
    case 0x00AE: /* REGISTERED SIGN */
    case 0x00B0: /* DEGREE SIGN */
    case 0x00B1: /* PLUS-MINUS SIGN */
    case 0x00B5: /* MICRO SIGN */
    case 0x00B6: /* PILCROW SIGN */
    case 0x00B7: /* MIDDLE DOT */
    case 0x00BB: /* RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK */

    /* Additional Cyrillic capital letters */
    case 0x0401:
    case 0x0402:
    case 0x0403:
    case 0x0404:
    case 0x0405:
    case 0x0406:
    case 0x0407:
    case 0x0408:
    case 0x0409:
    case 0x040A:
    case 0x040B:
    case 0x040C:
    case 0x040E:
    case 0x040F:

    /* Additional Cyrillic small letters */
    case 0x0451:
    case 0x0452:
    case 0x0453:
    case 0x0454:
    case 0x0455:
    case 0x0456:
    case 0x0457:
    case 0x0458:
    case 0x0459:
    case 0x045A:
    case 0x045B:
    case 0x045C:
    case 0x045E:
    case 0x045F:

    /* Cyrillic extensions */
    case 0x0490:
    case 0x0491:

    /* Typography and symbols present in cp1251 */
    case 0x2013: /* EN DASH */
    case 0x2014: /* EM DASH */
    case 0x2018: /* LEFT SINGLE QUOTATION MARK */
    case 0x2019: /* RIGHT SINGLE QUOTATION MARK */
    case 0x201A: /* SINGLE LOW-9 QUOTATION MARK */
    case 0x201C: /* LEFT DOUBLE QUOTATION MARK */
    case 0x201D: /* RIGHT DOUBLE QUOTATION MARK */
    case 0x201E: /* DOUBLE LOW-9 QUOTATION MARK */
    case 0x2020: /* DAGGER */
    case 0x2021: /* DOUBLE DAGGER */
    case 0x2022: /* BULLET */
    case 0x2026: /* HORIZONTAL ELLIPSIS */
    case 0x2030: /* PER MILLE SIGN */
    case 0x2039: /* SINGLE LEFT-POINTING ANGLE QUOTATION MARK */
    case 0x203A: /* SINGLE RIGHT-POINTING ANGLE QUOTATION MARK */
    case 0x20AC: /* EURO SIGN */
    case 0x2116: /* NUMERO SIGN */
    case 0x2122: /* TRADE MARK SIGN */
        return 1;

    default:
        return 0;
    }
}

static int is_allowed(struct uc_ctx *c, uint32_t cp)
{
    if (is_preserved(cp))
        return 1;

    if (is_ascii_printable(cp))
        return 1;

    switch (c->opts.enc)
    {
    case UC_ENC_ASCII:
        return 0;

    case UC_ENC_CP1252:
        return is_allowed_cp1252(cp);

    case UC_ENC_CP1251:
        return is_allowed_cp1251(cp);
    }

    return 0;
}

static int is_bad(struct uc_ctx *c, uint32_t cp)
{
    if (is_preserved(cp))
        return 0;

    if (is_control(cp))
        return 1;

    if (is_invisible(cp))
        return 1;

    if (is_ascii_printable(cp))
        return 0;

    if (!is_allowed(c, cp))
        return 1;

    return 0;
}

/*
 * Clear policy:
 *
 *   1. \n \r \t and ASCII printable pass unchanged.
 *   2. Controls are deleted.
 *   3. Zero-width/format/combining characters are deleted.
 *   4. Unicode spaces become normal ASCII space.
 *   5. Typography is normalized to ASCII always.
 *   6. Characters allowed by target encoding and not normalized pass unchanged.
 *   7. Bad visible characters are folded if possible, otherwise replaced by ?.
 */

static const char *map_typography(uint32_t cp)
{
    switch (cp)
    {
    /* Dashes and minus-like characters */
    case 0x2010: /* HYPHEN */
    case 0x2011: /* NON-BREAKING HYPHEN */
    case 0x2012: /* FIGURE DASH */
    case 0x2013: /* EN DASH */
    case 0x2014: /* EM DASH */
    case 0x2015: /* HORIZONTAL BAR */
    case 0x2212: /* MINUS SIGN */
    case 0xFE58: /* SMALL EM DASH */
    case 0xFE63: /* SMALL HYPHEN-MINUS */
        return "-";

    /* Single quotes and apostrophe-like characters */
    case 0x2018: /* LEFT SINGLE QUOTATION MARK */
    case 0x2019: /* RIGHT SINGLE QUOTATION MARK */
    case 0x201A: /* SINGLE LOW-9 QUOTATION MARK */
    case 0x201B: /* SINGLE HIGH-REVERSED QUOTATION MARK */
    case 0x2039: /* SINGLE LEFT-POINTING ANGLE QUOTATION MARK */
    case 0x203A: /* SINGLE RIGHT-POINTING ANGLE QUOTATION MARK */
    case 0x2032: /* PRIME */
        return "'";

    /* Double quotes */
    case 0x201C: /* LEFT DOUBLE QUOTATION MARK */
    case 0x201D: /* RIGHT DOUBLE QUOTATION MARK */
    case 0x201E: /* DOUBLE LOW-9 QUOTATION MARK */
    case 0x201F: /* DOUBLE HIGH-REVERSED QUOTATION MARK */
    case 0x00AB: /* LEFT-POINTING DOUBLE ANGLE QUOTATION MARK */
    case 0x00BB: /* RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK */
    case 0x2033: /* DOUBLE PRIME */
    case 0x2036: /* REVERSED DOUBLE PRIME */
        return "\"";

    /* Ellipsis */
    case 0x2026: /* HORIZONTAL ELLIPSIS */
        return "...";

    default:
        return NULL;
    }
}

static const char *map_fold(uint32_t cp)
{
    switch (cp)
    {
    /* Some spacing/modifier characters */
    case 0x00A8: /* DIAERESIS */
        return "\"";

    case 0x00AF: /* MACRON */
        return "-";

    case 0x00B4: /* ACUTE ACCENT */
        return "'";

    case 0x00B8: /* CEDILLA */
        return ",";

    /* Simple symbol approximations */
    case 0x00B1: /* PLUS-MINUS SIGN */
        return "+-";

    case 0x00B5: /* MICRO SIGN */
        return "u";

    case 0x00B7: /* MIDDLE DOT */
        return ".";

    case 0x00D7: /* MULTIPLICATION SIGN */
        return "x";

    case 0x00F7: /* DIVISION SIGN */
        return "/";

    case 0x2022: /* BULLET */
        return "*";

    case 0x2030: /* PER MILLE SIGN */
        return "0/00";

    case 0x2122: /* TRADE MARK SIGN */
        return "TM";

    /* Latin-1 Supplement letters */
    case 0x00C0:
    case 0x00C1:
    case 0x00C2:
    case 0x00C3:
    case 0x00C4:
    case 0x00C5:
        return "A";

    case 0x00C6:
        return "AE";

    case 0x00C7:
        return "C";

    case 0x00C8:
    case 0x00C9:
    case 0x00CA:
    case 0x00CB:
        return "E";

    case 0x00CC:
    case 0x00CD:
    case 0x00CE:
    case 0x00CF:
        return "I";

    case 0x00D0:
        return "D";

    case 0x00D1:
        return "N";

    case 0x00D2:
    case 0x00D3:
    case 0x00D4:
    case 0x00D5:
    case 0x00D6:
    case 0x00D8:
        return "O";

    case 0x00D9:
    case 0x00DA:
    case 0x00DB:
    case 0x00DC:
        return "U";

    case 0x00DD:
        return "Y";

    case 0x00DE:
        return "TH";

    case 0x00DF:
        return "ss";

    case 0x00E0:
    case 0x00E1:
    case 0x00E2:
    case 0x00E3:
    case 0x00E4:
    case 0x00E5:
        return "a";

    case 0x00E6:
        return "ae";

    case 0x00E7:
        return "c";

    case 0x00E8:
    case 0x00E9:
    case 0x00EA:
    case 0x00EB:
        return "e";

    case 0x00EC:
    case 0x00ED:
    case 0x00EE:
    case 0x00EF:
        return "i";

    case 0x00F0:
        return "d";

    case 0x00F1:
        return "n";

    case 0x00F2:
    case 0x00F3:
    case 0x00F4:
    case 0x00F5:
    case 0x00F6:
    case 0x00F8:
        return "o";

    case 0x00F9:
    case 0x00FA:
    case 0x00FB:
    case 0x00FC:
        return "u";

    case 0x00FD:
    case 0x00FF:
        return "y";

    case 0x00FE:
        return "th";

    /* Common Latin Extended characters relevant to cp1252 */
    case 0x0152: /* LATIN CAPITAL LIGATURE OE */
        return "OE";

    case 0x0153: /* LATIN SMALL LIGATURE OE */
        return "oe";

    case 0x0160: /* LATIN CAPITAL LETTER S WITH CARON */
        return "S";

    case 0x0161: /* LATIN SMALL LETTER S WITH CARON */
        return "s";

    case 0x0178: /* LATIN CAPITAL LETTER Y WITH DIAERESIS */
        return "Y";

    case 0x017D: /* LATIN CAPITAL LETTER Z WITH CARON */
        return "Z";

    case 0x017E: /* LATIN SMALL LETTER Z WITH CARON */
        return "z";

    case 0x0192: /* LATIN SMALL LETTER F WITH HOOK */
        return "f";

    case 0x02C6: /* MODIFIER LETTER CIRCUMFLEX ACCENT */
        return "^";

    case 0x02DC: /* SMALL TILDE */
        return "~";

    default:
        return NULL;
    }
}

static int fullwidth_to_ascii(uint32_t cp, char *out)
{
    /*
     * Fullwidth ASCII variants:
     * U+FF01..U+FF5E correspond to U+0021..U+007E.
     */
    if (cp >= 0xFF01 && cp <= 0xFF5E)
    {
        *out = (char)(cp - 0xFEE0);
        return 1;
    }

    return 0;
}

static int emit_unmapped_replacement(const struct uc_ctx *c, uc_write_fn out,
                                     void *arg)
{
    if (c->opts.use_color && c->opts.mode == UC_MODE_CLEAR)
        return emits_color(c, out, arg, "?");

    return emits(out, arg, "?");
}

static int handle_invalid(struct uc_ctx *c, uc_write_fn out, void *arg)
{
    c->issues++;

    switch (c->opts.mode)
    {
    case UC_MODE_SHOW:
        return emits_color(c, out, arg, "[INVALID]");

    case UC_MODE_CLEAR:
        return emits_color(c, out, arg, "?");

    case UC_MODE_HIGHLIGHT:
    default:
        if (c->opts.use_color)
            return emit_cp_color(c, out, arg, 0xFFFD);

        return emit_cp_plain(out, arg, 0xFFFD);
    }
}

static int handle_cp(struct uc_ctx *c, uint32_t cp, uc_write_fn out,
                     void *arg)
{
    /*
     * Clear mode has its own policy.
     */
    if (c->opts.mode == UC_MODE_CLEAR)
    {
        if (is_preserved(cp) || is_ascii_printable(cp))
            return emit_cp_plain(out, arg, cp);

        if (is_control(cp))
        {
            c->issues++;
            return 0;
        }

        if (is_zero_width_or_format(cp) || is_combining_mark(cp))
        {
            c->issues++;
            return 0;
        }

        if (is_unicode_space(cp))
        {
            c->issues++;
            return emits(out, arg, " ");
        }

        const char *typo = map_typography(cp);
        if (typo)
        {
            c->issues++;
            return emits(out, arg, typo);
        }

        if (!is_bad(c, cp))
            return emit_cp_plain(out, arg, cp);

        c->issues++;

        char fw;
        if (fullwidth_to_ascii(cp, &fw))
            return emit(out, arg, &fw, 1);

        const char *folded = map_fold(cp);
        if (folded)
            return emits(out, arg, folded);

        return emit_unmapped_replacement(c, out, arg);
    }

    /*
     * Show and highlight modes.
     */
    if (!is_bad(c, cp))
        return emit_cp_plain(out, arg, cp);

    c->issues++;

    switch (c->opts.mode)
    {
    case UC_MODE_SHOW:
        return emit_show_cp(c, out, arg, cp);

    case UC_MODE_HIGHLIGHT:
    default:
        if (is_control(cp) || is_invisible(cp))
        {
            if (c->opts.use_color)
                return emit_cp_color(c, out, arg, 0xFFFD);

            return emit_cp_plain(out, arg, 0xFFFD);
        }

        if (!c->opts.use_color)
            return emit_cp_plain(out, arg, cp);

        return emit_cp_color(c, out, arg, cp);
    }
}

/*
 * UTF-8 decoder.
 */

static int utf8_start(struct uc_ctx *c, unsigned char b, uint32_t *cp)
{
    c->need = 0;
    c->cp = 0;
    c->lower = 0;
    c->upper = 0;

    if (b < 0x80)
    {
        *cp = b;
        return UTF8_DONE;
    }

    if (b >= 0xC2 && b <= 0xDF)
    {
        c->cp = b & 0x1F;
        c->lower = 0x80;
        c->upper = 0x7FF;
        c->need = 1;
        return UTF8_STARTED;
    }

    if (b >= 0xE0 && b <= 0xEF)
    {
        c->cp = b & 0x0F;
        c->need = 2;

        if (b == 0xE0)
        {
            c->lower = 0x800;
            c->upper = 0xFFFF;
        }
        else if (b == 0xED)
        {
            c->lower = 0x800;
            c->upper = 0xD7FF;
        }
        else if (b == 0xEE || b == 0xEF)
        {
            c->lower = 0xE000;
            c->upper = 0xFFFF;
        }
        else
        {
            c->lower = 0x800;
            c->upper = 0xFFFF;
        }

        return UTF8_STARTED;
    }

    if (b >= 0xF0 && b <= 0xF4)
    {
        c->cp = b & 0x07;
        c->lower = 0x10000;
        c->upper = 0x10FFFF;
        c->need = 3;
        return UTF8_STARTED;
    }

    return UTF8_BAD_START;
}

static int utf8_continue(struct uc_ctx *c, unsigned char b, uint32_t *cp)
{
    if (b < 0x80 || b > 0xBF)
        return UTF8_BAD_CONT;

    c->cp = (c->cp << 6) | (b & 0x3F);
    c->need--;

    if (c->need == 0)
    {
        if (c->cp < c->lower || c->cp > c->upper)
            return UTF8_BAD_SEQ;

        *cp = c->cp;
        return UTF8_DONE;
    }

    return UTF8_STARTED;
}

int uc_feed(struct uc_ctx *c, const void *buf, size_t len,
            uc_write_fn out, void *arg)
{
    const unsigned char *p = buf;
    size_t i;

    for (i = 0; i < len; i++)
    {
        unsigned char b = p[i];

    retry:
        if (c->need == 0)
        {
            uint32_t cp;
            int r;

            r = utf8_start(c, b, &cp);

            if (r == UTF8_BAD_START)
            {
                if (handle_invalid(c, out, arg) < 0)
                    return -1;

                continue;
            }

            if (r == UTF8_STARTED)
                continue;

            if (handle_cp(c, cp, out, arg) < 0)
                return -1;
        }
        else
        {
            uint32_t cp;
            int r;

            r = utf8_continue(c, b, &cp);

            if (r == UTF8_BAD_CONT)
            {
                c->need = 0;

                if (handle_invalid(c, out, arg) < 0)
                    return -1;

                goto retry;
            }

            if (r == UTF8_BAD_SEQ)
            {
                c->need = 0;

                if (handle_invalid(c, out, arg) < 0)
                    return -1;

                continue;
            }

            if (r == UTF8_DONE)
            {
                if (handle_cp(c, cp, out, arg) < 0)
                    return -1;
            }
        }
    }

    return 0;
}

int uc_end(struct uc_ctx *c, uc_write_fn out, void *arg)
{
    if (c->need != 0)
    {
        c->need = 0;
        return handle_invalid(c, out, arg);
    }

    return 0;
}