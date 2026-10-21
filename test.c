#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uc.h"

static int tests_run;
static int tests_failed;

struct cap
{
  char buf[8192];
  size_t len;
};

static int cap_write(const char *buf, size_t len, void *arg)
{
  struct cap *c = arg;

  if (c->len + len >= sizeof(c->buf))
    return -1;

  memcpy(c->buf + c->len, buf, len);
  c->len += len;
  c->buf[c->len] = '\0';

  return 0;
}

static void print_mem(const char *label, const char *p, size_t n)
{
  size_t i;

  fprintf(stderr, "  %s: ", label);

  for (i = 0; i < n; i++)
    fprintf(stderr, "\\x%02X", (unsigned char)p[i]);

  fprintf(stderr, "\n");
}

static int run_uc_full(enum uc_mode mode, enum uc_enc enc, int color,
                       int fold, const char *in, size_t inlen,
                       char *out, size_t outcap, size_t *outlen,
                       unsigned long *issues)
{
  struct uc_opts opts;
  struct cap cap;
  struct uc_ctx *ctx;

  memset(&opts, 0, sizeof(opts));
  memset(&cap, 0, sizeof(cap));

  opts.mode = mode;
  opts.enc = enc;
  opts.use_color = color;
  opts.fold_diacritics = fold;

  ctx = uc_new(&opts);
  if (!ctx)
    return -1;

  if (uc_feed(ctx, in, inlen, cap_write, &cap) < 0)
  {
    uc_free(ctx);
    return -1;
  }

  if (uc_end(ctx, cap_write, &cap) < 0)
  {
    uc_free(ctx);
    return -1;
  }

  if (cap.len >= outcap)
  {
    uc_free(ctx);
    return -1;
  }

  memcpy(out, cap.buf, cap.len);
  out[cap.len] = '\0';

  if (outlen)
    *outlen = cap.len;

  if (issues)
    *issues = uc_issues(ctx);

  uc_free(ctx);
  return 0;
}

static void check_case(const char *name,
                       enum uc_mode mode,
                       enum uc_enc enc,
                       int color,
                       int fold,
                       const char *in,
                       size_t inlen,
                       const char *expected,
                       size_t explen,
                       unsigned long exp_issues)
{
  char out[8192];
  size_t outlen = 0;
  unsigned long issues = 0;

  tests_run++;

  if (run_uc_full(mode, enc, color, fold, in, inlen,
                  out, sizeof(out), &outlen, &issues) != 0)
  {
    tests_failed++;
    fprintf(stderr, "FAIL %s: run error\n", name);
    return;
  }

  if (outlen != explen ||
      memcmp(out, expected, explen) != 0 ||
      issues != exp_issues)
  {
    tests_failed++;

    fprintf(stderr,
            "FAIL %s: got len %zu issues %lu, expected len %zu issues %lu\n",
            name, outlen, issues, explen, exp_issues);

    print_mem("got", out, outlen);
    print_mem("expected", expected, explen);
  }
}

static void check_two_chunks(const char *name,
                             enum uc_mode mode,
                             enum uc_enc enc,
                             const char *chunk1,
                             size_t chunk1_len,
                             const char *chunk2,
                             size_t chunk2_len,
                             const char *expected,
                             size_t explen,
                             unsigned long exp_issues)
{
  struct uc_opts opts;
  struct cap cap;
  struct uc_ctx *ctx;
  unsigned long issues;

  memset(&opts, 0, sizeof(opts));
  memset(&cap, 0, sizeof(cap));

  opts.mode = mode;
  opts.enc = enc;
  opts.use_color = 0;

  tests_run++;

  ctx = uc_new(&opts);
  if (!ctx)
  {
    tests_failed++;
    fprintf(stderr, "FAIL %s: uc_new() failed\n", name);
    return;
  }

  if (uc_feed(ctx, chunk1, chunk1_len, cap_write, &cap) < 0 ||
      uc_feed(ctx, chunk2, chunk2_len, cap_write, &cap) < 0 ||
      uc_end(ctx, cap_write, &cap) < 0)
  {
    tests_failed++;
    fprintf(stderr, "FAIL %s: feed/end error\n", name);
    uc_free(ctx);
    return;
  }

  issues = uc_issues(ctx);
  uc_free(ctx);

  if (cap.len != explen ||
      memcmp(cap.buf, expected, explen) != 0 ||
      issues != exp_issues)
  {
    tests_failed++;

    fprintf(stderr,
            "FAIL %s: got len %zu issues %lu, expected len %zu issues %lu\n",
            name, cap.len, issues, explen, exp_issues);

    print_mem("got", cap.buf, cap.len);
    print_mem("expected", expected, explen);
  }
}

#define T(name, mode, enc, color, input, expected, issues)     \
  check_case(name, mode, enc, color, 0, input, sizeof(input) - 1, \
             expected, sizeof(expected) - 1, issues)

#define TF(name, mode, enc, color, input, expected, issues)    \
  check_case(name, mode, enc, color, 1, input, sizeof(input) - 1, \
             expected, sizeof(expected) - 1, issues)

#define T2(name, mode, enc, chunk1, chunk2, expected, issues)   \
  check_two_chunks(name, mode, enc, chunk1, sizeof(chunk1) - 1, \
                   chunk2, sizeof(chunk2) - 1,                  \
                   expected, sizeof(expected) - 1, issues)

/*
 * UTF-8 encoder, so a test can name a code point instead of its bytes.
 */
static size_t utf8_encode(uint32_t cp, unsigned char *out)
{
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

/*
 * An allowed code point must pass through, a rejected one must be shown.
 */
static void check_code_point(const char *name, enum uc_enc enc,
                             uint32_t cp, int allowed)
{
  char label[64];
  char expected[16];
  unsigned char in[4];
  size_t inlen;
  int n;

  snprintf(label, sizeof(label), "%s U+%04X", name, (unsigned)cp);

  inlen = utf8_encode(cp, in);

  if (allowed)
  {
    memcpy(expected, in, inlen);
    n = (int)inlen;
  }
  else
  {
    n = snprintf(expected, sizeof(expected), "[U+%04X]", (unsigned)cp);
  }

  check_case(label, UC_MODE_SHOW, enc, 0, 0, (const char *)in, inlen,
             expected, (size_t)n, allowed ? 0 : 1);
}

static void check_encoding(const char *name, enum uc_enc enc,
                            const uint32_t *allowed, size_t nallowed,
                            const uint32_t *rejected, size_t nrejected)
{
  size_t i;

  for (i = 0; i < nallowed; i++)
    check_code_point(name, enc, allowed[i], 1);

  for (i = 0; i < nrejected; i++)
    check_code_point(name, enc, rejected[i], 0);
}

/*
 * An invisible code point is deleted in clear mode. A visible code point
 * outside the encoding is replaced instead, so the output tells the two
 * apart.
 */
static void check_invisible_code_point(const char *name, uint32_t cp)
{
  char label[64];
  unsigned char in[4];
  size_t inlen;

  snprintf(label, sizeof(label), "%s U+%04X", name, (unsigned)cp);

  inlen = utf8_encode(cp, in);

  check_case(label, UC_MODE_CLEAR, UC_ENC_ASCII, 0, 0, (const char *)in, inlen,
             "", 0, 1);
}

/*
 * Every code point in a range must be invisible.
 */
static void check_invisible_ranges(const char *name,
                                   const uint32_t *ranges, size_t nranges)
{
  size_t i;

  for (i = 0; i < nranges; i += 2)
  {
    uint32_t cp;

    for (cp = ranges[i]; cp <= ranges[i + 1]; cp++)
      check_invisible_code_point(name, cp);
  }
}

/*
 * ASCII base letters of Latin letters with diacritics, one per code point.
 * A dot marks a code point with no diacritic base.
 *
 * The blocks are U+00C0..U+024F and U+1E00..U+1EFF.
 */
static const char diacritic_latin[] =
  "AAAAAA.CEEEEIIII.NOOOOO..UUUUY..aaaaaa.ceeeeiiii.nooooo..uuuuy.y"
  "AaAaAaCcCcCcCcDd..EeEeEeEeEeGgGgGgGgHh..IiIiIiIiI...JjKk.LlLlLl."
  "...NnNnNn...OoOoOo..RrRrRrSsSsSsSsTtTt..UuUuUuUuUuUuWwYyYZzZzZz."
  "................................Oo.............Uu..............."
  ".............AaIiOoUuUuUuUuUu.AaAa....GgKkOoOo..j...Gg..NnAa...."
  "AaAaEeEeIiIiOoOoRrRrUuUuSsTt..Hh......AaEeOoOoOoOoYy............"
  "................";

static const char diacritic_extended[] =
  "AaBbBbBbCcDdDdDdDdDdEeEeEeEeEeFfGgHhHhHhHhHhIiIiKkKkKkLlLlLlLlMm"
  "MmMmNnNnNnNnOoOoOoOoPpPpRrRrRrRrSsSsSsSsSsTtTtTtTtUuUuUuUuUuVvVv"
  "WwWwWwWwWwXxXxYyZzZzZzhtwy......AaAaAaAaAaAaAaAaAaAaAaAaEeEeEeEe"
  "EeEeEeEeIiIiOoOoOoOoOoOoOoOoOoOoOoOoUuUuUuUuUuUuUuYyYyYyYy......";

/*
 * With --fold-diacritics a letter with diacritics folds to its ASCII base
 * even when the target encoding allows the character.
 *
 * A code point without a base letter keeps the output it has without the
 * flag.
 */
static void check_diacritic_table(const char *name, const char *table,
                                  uint32_t first, size_t len)
{
  size_t i;

  for (i = 0; i < len; i++)
  {
    char label[64];
    unsigned char in[4];
    size_t inlen;

    snprintf(label, sizeof(label), "%s U+%04X", name,
             (unsigned)(first + (uint32_t)i));

    inlen = utf8_encode(first + (uint32_t)i, in);

    if (table[i] == '.')
    {
      char with_flag[16];
      char without_flag[16];
      size_t len_with = 0;
      size_t len_without = 0;

      tests_run++;

      if (run_uc_full(UC_MODE_CLEAR, UC_ENC_CP1252, 0, 1,
                      (const char *)in, inlen,
                      with_flag, sizeof(with_flag), &len_with, NULL) != 0 ||
          run_uc_full(UC_MODE_CLEAR, UC_ENC_CP1252, 0, 0,
                      (const char *)in, inlen,
                      without_flag, sizeof(without_flag), &len_without, NULL) != 0)
      {
        tests_failed++;
        fprintf(stderr, "FAIL %s: run error\n", label);
        continue;
      }

      if (len_with != len_without ||
          memcmp(with_flag, without_flag, len_with) != 0)
      {
        tests_failed++;
        fprintf(stderr, "FAIL %s: flag changed output\n", label);
        print_mem("got", with_flag, len_with);
        print_mem("expected", without_flag, len_without);
      }

      continue;
    }

    check_case(label, UC_MODE_CLEAR, UC_ENC_CP1252, 0, 1,
               (const char *)in, inlen, &table[i], 1, 1);
  }
}

/*
 * The full windows-1252 and cp1251 code point sets.
 *
 * U+00A0 and U+00AD belong to both encodings but are invisible, so they
 * are reported as problems whatever the encoding is.
 */
static const uint32_t cp1252_allowed[] = {
  0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7, 0x00A8, 0x00A9,
  0x00AA, 0x00AB, 0x00AC, 0x00AE, 0x00AF, 0x00B0, 0x00B1, 0x00B2, 0x00B3,
  0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC,
  0x00BD, 0x00BE, 0x00BF, 0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5,
  0x00C6, 0x00C7, 0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE,
  0x00CF, 0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7,
  0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF, 0x00E0,
  0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7, 0x00E8, 0x00E9,
  0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF, 0x00F0, 0x00F1, 0x00F2,
  0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7, 0x00F8, 0x00F9, 0x00FA, 0x00FB,
  0x00FC, 0x00FD, 0x00FE, 0x00FF, 0x0152, 0x0153, 0x0160, 0x0161, 0x0178,
  0x017D, 0x017E, 0x0192, 0x02C6, 0x02DC, 0x2013, 0x2014, 0x2018, 0x2019,
  0x201A, 0x201C, 0x201D, 0x201E, 0x2020, 0x2021, 0x2022, 0x2026, 0x2030,
  0x2039, 0x203A, 0x20AC, 0x2122,
};

static const uint32_t cp1251_allowed[] = {
  0x00A4, 0x00A6, 0x00A7, 0x00A9, 0x00AB, 0x00AC, 0x00AE, 0x00B0, 0x00B1,
  0x00B5, 0x00B6, 0x00B7, 0x00BB, 0x0401, 0x0402, 0x0403, 0x0404, 0x0405,
  0x0406, 0x0407, 0x0408, 0x0409, 0x040A, 0x040B, 0x040C, 0x040E, 0x040F,
  0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418,
  0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F, 0x0420, 0x0421,
  0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A,
  0x042B, 0x042C, 0x042D, 0x042E, 0x042F, 0x0430, 0x0431, 0x0432, 0x0433,
  0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C,
  0x043D, 0x043E, 0x043F, 0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445,
  0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E,
  0x044F, 0x0451, 0x0452, 0x0453, 0x0454, 0x0455, 0x0456, 0x0457, 0x0458,
  0x0459, 0x045A, 0x045B, 0x045C, 0x045E, 0x045F, 0x0490, 0x0491, 0x2013,
  0x2014, 0x2018, 0x2019, 0x201A, 0x201C, 0x201D, 0x201E, 0x2020, 0x2021,
  0x2022, 0x2026, 0x2030, 0x2039, 0x203A, 0x20AC, 0x2116, 0x2122,
};

static const uint32_t cp1252_rejected[] = {
  0x0100, 0x0101, 0x0131, 0x02C7, 0x0391, 0x03B1, 0x0410, 0x0430, 0x05D0,
  0x0627, 0x0915, 0x2010, 0x2011, 0x2012, 0x2015, 0x2032, 0x2033, 0x2036,
  0x2103, 0x2126, 0x2212, 0x2500, 0x4E00, 0x1F600,
};

static const uint32_t cp1251_rejected[] = {
  0x00C0, 0x00E9, 0x0100, 0x0152, 0x0153, 0x0160, 0x0178, 0x0192, 0x0391,
  0x03B1, 0x2010, 0x2012, 0x2015, 0x2032, 0x2126, 0x2500, 0x4E00, 0x1F600,
};

/*
 * Format characters the range table must cover. Every one of them is
 * invisible, so every one of them is a problem in any encoding.
 *
 * The table holds range endpoints: every code point from lo to hi must
 * be reported.
 */
static const uint32_t format_ranges[] = {
  0x00AD, 0x00AD, 0x034F, 0x034F, 0x0600, 0x0605, 0x061C, 0x061C,
  0x06DD, 0x06DD, 0x070F, 0x070F, 0x0890, 0x0891, 0x08E2, 0x08E2,
  0x110BD, 0x110BD, 0x110CD, 0x110CD, 0x13430, 0x1343F, 0x1BCA0,
  0x1BCA3, 0x1D173, 0x1D17A, 0x180E, 0x180E, 0x200B, 0x200F,
  0x2028, 0x202E, 0x2060, 0x2064, 0x2066, 0x206F, 0xFE00, 0xFE0F,
  0xFEFF, 0xFEFF, 0xFFF9, 0xFFFB, 0xE0001, 0xE0001, 0xE0020, 0xE007F,
  0xE0100, 0xE01EF,
};

/*
 * Combining marks the range table must cover.
 */
static const uint32_t combining_ranges[] = {
  0x0300, 0x036F, 0x0483, 0x0489, 0x1AB0, 0x1AFF, 0x1DC0, 0x1DFF,
  0x20D0, 0x20FF, 0x302A, 0x302F, 0x3099, 0x309A, 0xA66F, 0xA672,
  0xFE20, 0xFE2F,
};

int main(void)
{
  /*
   * Empty input.
   */
  T("empty clear", UC_MODE_CLEAR, UC_ENC_ASCII, 0, "", "", 0);
  T("empty show", UC_MODE_SHOW, UC_ENC_ASCII, 0, "", "", 0);
  T("empty highlight", UC_MODE_HIGHLIGHT, UC_ENC_ASCII, 0, "", "", 0);

  /*
   * ASCII passthrough.
   */
  T("ascii clear",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "abc XYZ 012 .,!?~\n\t\r",
    "abc XYZ 012 .,!?~\n\t\r",
    0);

  T("ascii show",
    UC_MODE_SHOW, UC_ENC_ASCII, 0,
    "abc XYZ 012 .,!?~\n\t\r",
    "abc XYZ 012 .,!?~\n\t\r",
    0);

  T("ascii highlight",
    UC_MODE_HIGHLIGHT, UC_ENC_ASCII, 0,
    "abc XYZ 012 .,!?~\n\t\r",
    "abc XYZ 012 .,!?~\n\t\r",
    0);

  /*
   * Preserved structural characters.
   */
  T("preserve newline/tab/cr in clear",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\n\r\t",
    "\n\r\t",
    0);

  /*
   * ASCII controls.
   */
  T("clear deletes C0 controls",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "a"
    "\x01"
    "b"
    "\x1b"
    "c",
    "abc",
    2);

  T("clear deletes DEL",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "a"
    "\x7f"
    "b",
    "ab",
    1);

  T("clear deletes NUL",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "a"
    "\x00"
    "b",
    "ab",
    1);

  T("show prints C0 control",
    UC_MODE_SHOW, UC_ENC_ASCII, 0,
    "a"
    "\x01"
    "b",
    "a[U+0001]b",
    1);

  T("show prints DEL",
    UC_MODE_SHOW, UC_ENC_ASCII, 0,
    "a"
    "\x7f"
    "b",
    "a[U+007F]b",
    1);

  T("highlight replaces control with U+FFFD when color disabled",
    UC_MODE_HIGHLIGHT, UC_ENC_ASCII, 0,
    "a"
    "\x01"
    "b",
    "a"
    "\xEF\xBF\xBD"
    "b",
    1);

  /*
   * C1 controls.
   */
  T("clear deletes C1 control U+0080",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC2\x80",
    "",
    1);

  T("show prints C1 control U+0080",
    UC_MODE_SHOW, UC_ENC_ASCII, 0,
    "\xC2\x80",
    "[U+0080]",
    1);

  /*
   * Invalid UTF-8.
   */
  T("invalid byte in clear becomes ?",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xFF",
    "?",
    1);

  T("invalid byte in show becomes [INVALID]",
    UC_MODE_SHOW, UC_ENC_ASCII, 0,
    "\xFF",
    "[INVALID]",
    1);

  T("invalid byte in highlight becomes U+FFFD when color disabled",
    UC_MODE_HIGHLIGHT, UC_ENC_ASCII, 0,
    "\xFF",
    "\xEF\xBF\xBD",
    1);

  T("truncated UTF-8 sequence becomes ?",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC3",
    "?",
    1);

  T("invalid continuation byte",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC3\x28",
    "?(",
    1);

  T("valid char then truncated sequence",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "a"
    "\xC3",
    "a?",
    1);

  T("overlong sequence counts as one invalid",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE0\x80\x80",
    "?",
    1);

  T("surrogate sequence counts as one invalid",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xED\xA0\x80",
    "?",
    1);

  /*
   * Split UTF-8 sequences across uc_feed() calls.
   */
  T2("split UTF-8 sequence: e acute in show",
     UC_MODE_SHOW, UC_ENC_ASCII,
     "\xC3", "\xA9",
     "[U+00E9]",
     1);

  T2("split UTF-8 sequence: e acute kept by cp1252 in clear",
     UC_MODE_CLEAR, UC_ENC_CP1252,
     "\xC3", "\xA9",
     "\xC3\xA9",
     0);

  T2("split UTF-8 sequence: emoji in show",
     UC_MODE_SHOW, UC_ENC_ASCII,
     "\xF0\x9F", "\x98\x80",
     "[U+1F600]",
     1);

  /*
   * Show mode formatting.
   */
  T("show NBSP",
    UC_MODE_SHOW, UC_ENC_ASCII, 0,
    "\xC2\xA0",
    "[U+00A0]",
    1);

  T("show zero width space",
    UC_MODE_SHOW, UC_ENC_ASCII, 0,
    "\xE2\x80\x8B",
    "[U+200B]",
    1);

  T("show emoji uses minimal width code",
    UC_MODE_SHOW, UC_ENC_ASCII, 0,
    "\xF0\x9F\x98\x80",
    "[U+1F600]",
    1);

  T("show combining acute accent",
    UC_MODE_SHOW, UC_ENC_ASCII, 0,
    "\xCC\x81",
    "[U+0301]",
    1);

  T("show fullwidth A",
    UC_MODE_SHOW, UC_ENC_ASCII, 0,
    "\xEF\xBC\xA1",
    "[U+FF21]",
    1);

  /*
   * Hidden spaces in clear.
   */
  T("clear NBSP becomes space",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC2\xA0",
    " ",
    1);

  T("clear EN QUAD becomes space",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\x80",
    " ",
    1);

  T("clear IDEOGRAPHIC SPACE becomes space",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE3\x80\x80",
    " ",
    1);

  /*
   * Zero-width and format characters in clear.
   */
  T("clear ZERO WIDTH SPACE deletes",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\x8B",
    "",
    1);

  T("clear ZERO WIDTH NON-JOINER deletes",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\x8C",
    "",
    1);

  T("clear ZERO WIDTH JOINER deletes",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\x8D",
    "",
    1);

  T("clear BOM deletes",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xEF\xBB\xBF",
    "",
    1);

  T("clear SOFT HYPHEN deletes",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC2\xAD",
    "",
    1);

  T("clear LINE SEPARATOR deletes",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\xA8",
    "",
    1);

  T("clear PARAGRAPH SEPARATOR deletes",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\xA9",
    "",
    1);

  T("clear RTL OVERRIDE deletes",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\xAE",
    "",
    1);

  T("clear VARIATION SELECTOR-16 deletes",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xEF\xB8\x8F",
    "",
    1);

  T("clear COMBINING GRAPHEME JOINER deletes",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xCD\x8F",
    "",
    1);

  T("clear MONGOLIAN VOWEL SEPARATOR deletes",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE1\xA0\x8E",
    "",
    1);

  T("clear INVISIBLE TIMES deletes",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x81\xAA",
    "",
    1);

  T("clear INTERLINEAR ANNOTATION ANCHOR deletes",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xEF\xBF\xB9",
    "",
    1);

  T("clear LANGUAGE TAG deletes",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xF3\xA0\x80\x81",
    "",
    1);

  /*
   * Combining marks.
   */
  T("clear deletes combining acute accent",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xCC\x81",
    "",
    1);

  T("clear deletes combining acute accent after e",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "e"
    "\xCC\x81",
    "e",
    1);

  T("highlight combining mark becomes U+FFFD when color disabled",
    UC_MODE_HIGHLIGHT, UC_ENC_ASCII, 0,
    "\xCC\x81",
    "\xEF\xBF\xBD",
    1);

  /*
   * Typography normalization in ASCII clear mode.
   */
  T("clear HYPHEN",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\x90",
    "-",
    1);

  T("clear NON-BREAKING HYPHEN",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\x91",
    "-",
    1);

  T("clear FIGURE DASH",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\x92",
    "-",
    1);

  T("clear EN DASH",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\x93",
    "-",
    1);

  T("clear EM DASH",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\x94",
    "-",
    1);

  T("clear HORIZONTAL BAR",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\x95",
    "-",
    1);

  T("clear MINUS SIGN",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x88\x92",
    "-",
    1);

  T("clear LEFT SINGLE QUOTATION MARK",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\x98",
    "\x27",
    1);

  T("clear RIGHT SINGLE QUOTATION MARK",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\x99",
    "\x27",
    1);

  T("clear SINGLE LOW-9 QUOTATION MARK",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\x9A",
    "\x27",
    1);

  T("clear SINGLE HIGH-REVERSED QUOTATION MARK",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\x9B",
    "\x27",
    1);

  T("clear SINGLE LEFT-POINTING ANGLE QUOTATION MARK",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\xB9",
    "\x27",
    1);

  T("clear SINGLE RIGHT-POINTING ANGLE QUOTATION MARK",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\xBA",
    "\x27",
    1);

  T("clear PRIME",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\xB2",
    "\x27",
    1);

  T("clear DOUBLE PRIME",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\xB3",
    "\x22",
    1);

  T("clear LEFT DOUBLE QUOTATION MARK",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\x9C",
    "\x22",
    1);

  T("clear RIGHT DOUBLE QUOTATION MARK",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\x9D",
    "\x22",
    1);

  T("clear DOUBLE LOW-9 QUOTATION MARK",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\x9E",
    "\x22",
    1);

  T("clear DOUBLE HIGH-REVERSED QUOTATION MARK",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\x9F",
    "\x22",
    1);

  T("clear LEFT-POINTING DOUBLE ANGLE QUOTATION MARK",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC2\xAB",
    "\x22",
    1);

  T("clear RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC2\xBB",
    "\x22",
    1);

  T("clear HORIZONTAL ELLIPSIS",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\xA6",
    "...",
    1);

  /*
   * Diacritics and ligatures in ASCII clear mode.
   */
  T("clear e acute to e",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC3\xA9",
    "e",
    1);

  T("clear c cedilla to c",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC3\xA7",
    "c",
    1);

  T("clear n tilde to n",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC3\xB1",
    "n",
    1);

  T("clear u diaeresis to u",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC3\xBC",
    "u",
    1);

  T("clear o stroke to o",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC3\xB8",
    "o",
    1);

  T("clear ae ligature to ae",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC3\xA6",
    "ae",
    1);

  T("clear oe ligature to oe",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC5\x93",
    "oe",
    1);

  T("clear capital OE ligature to OE",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC5\x92",
    "OE",
    1);

  T("clear sharp s to ss",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC3\x9F",
    "ss",
    1);

  T("clear capital thorn to TH",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC3\x9E",
    "TH",
    1);

  T("clear small thorn to th",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC3\xBE",
    "th",
    1);

  T("clear S caron to S",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC5\xA0",
    "S",
    1);

  T("clear s caron to s",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC5\xA1",
    "s",
    1);

  T("clear Z caron to Z",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC5\xBD",
    "Z",
    1);

  T("clear z caron to z",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC5\xBE",
    "z",
    1);

  T("clear capital Y diaeresis to Y",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC5\xB8",
    "Y",
    1);

  T("clear f hook to f",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC6\x92",
    "f",
    1);

  T("clear modifier circumflex accent to ^",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xCB\x86",
    "^",
    1);

  T("clear small tilde to ~",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xCB\x9C",
    "~",
    1);

  /*
   * Some symbol approximations in ASCII clear mode.
   */
  T("clear bullet to *",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\xA2",
    "*",
    1);

  T("clear copyright to (C)",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC2\xA9",
    "(C)",
    1);

  T("clear registered sign to (R)",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC2\xAE",
    "(R)",
    1);

  T("clear degree sign to deg",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC2\xB0",
    "deg",
    1);

  T("clear euro sign to EUR",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x82\xAC",
    "EUR",
    1);

  T("clear numero sign to No.",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x84\x96",
    "No.",
    1);

  T("clear per mille to 0/00",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x80\xB0",
    "0/00",
    1);

  T("clear trade mark to TM",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xE2\x84\xA2",
    "TM",
    1);

  T("clear plus-minus to +-",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC2\xB1",
    "+-",
    1);

  T("clear micro sign to u",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC2\xB5",
    "u",
    1);

  T("clear middle dot to .",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC2\xB7",
    ".",
    1);

  T("clear multiplication sign to x",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC3\x97",
    "x",
    1);

  T("clear division sign to /",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC3\xB7",
    "/",
    1);

  /*
   * Fullwidth ASCII folding.
   */
  T("clear fullwidth A",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xEF\xBC\xA1",
    "A",
    1);

  T("clear fullwidth a",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xEF\xBD\x81",
    "a",
    1);

  T("clear fullwidth 0",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xEF\xBC\x90",
    "0",
    1);

  T("clear fullwidth exclamation",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xEF\xBC\x81",
    "!",
    1);

  T("clear fullwidth hello",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xEF\xBD\x88"
    "\xEF\xBD\x85"
    "\xEF\xBD\x8C"
    "\xEF\xBD\x8C"
    "\xEF\xBD\x8F",
    "hello",
    5);

  /*
   * Unknown/unmapped visible characters in ASCII clear mode.
   */
  T("clear Cyrillic A in ascii becomes ?",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xD0\x90",
    "?",
    1);

  T("clear emoji in ascii becomes ?",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xF0\x9F\x98\x80",
    "?",
    1);

  /*
   * Encoding: cp1252.
   */
  T("cp1252 keeps e acute",
    UC_MODE_CLEAR, UC_ENC_CP1252, 0,
    "caf"
    "\xC3\xA9",
    "caf"
    "\xC3\xA9",
    0);

  T("cp1252 keeps oe ligature",
    UC_MODE_CLEAR, UC_ENC_CP1252, 0,
    "\xC5\x93",
    "\xC5\x93",
    0);

  T("cp1252 keeps euro sign",
    UC_MODE_CLEAR, UC_ENC_CP1252, 0,
    "\xE2\x82\xAC",
    "\xE2\x82\xAC",
    0);

  T("cp1252 keeps copyright sign",
    UC_MODE_CLEAR, UC_ENC_CP1252, 0,
    "\xC2\xA9",
    "\xC2\xA9",
    0);

  T("cp1252 still normalizes NBSP to space",
    UC_MODE_CLEAR, UC_ENC_CP1252, 0,
    "\xC2\xA0",
    " ",
    1);

  T("cp1252 deletes C1 control U+0080",
    UC_MODE_CLEAR, UC_ENC_CP1252, 0,
    "\xC2\x80",
    "",
    1);

  /*
   * Typography is normalized even if allowed by target encoding.
   */
  T("cp1252 normalizes EN DASH",
    UC_MODE_CLEAR, UC_ENC_CP1252, 0,
    "\xE2\x80\x93",
    "-",
    1);

  T("cp1252 normalizes LEFT DOUBLE QUOTATION MARK",
    UC_MODE_CLEAR, UC_ENC_CP1252, 0,
    "\xE2\x80\x9C",
    "\x22",
    1);

  T("cp1252 normalizes HORIZONTAL ELLIPSIS",
    UC_MODE_CLEAR, UC_ENC_CP1252, 0,
    "\xE2\x80\xA6",
    "...",
    1);

  /*
   * Encoding: cp1251.
   */
  T("cp1251 keeps basic Cyrillic",
    UC_MODE_CLEAR, UC_ENC_CP1251, 0,
    "\xD0\x90\xD1\x8F",
    "\xD0\x90\xD1\x8F",
    0);

  T("cp1251 keeps capital IO",
    UC_MODE_CLEAR, UC_ENC_CP1251, 0,
    "\xD0\x81",
    "\xD0\x81",
    0);

  T("cp1251 keeps small IO",
    UC_MODE_CLEAR, UC_ENC_CP1251, 0,
    "\xD1\x91",
    "\xD1\x91",
    0);

  T("cp1251 keeps small YI",
    UC_MODE_CLEAR, UC_ENC_CP1251, 0,
    "\xD1\x97",
    "\xD1\x97",
    0);

  T("cp1251 keeps numero sign",
    UC_MODE_CLEAR, UC_ENC_CP1251, 0,
    "\xE2\x84\x96",
    "\xE2\x84\x96",
    0);

  T("cp1251 still normalizes NBSP to space",
    UC_MODE_CLEAR, UC_ENC_CP1251, 0,
    "\xC2\xA0",
    " ",
    1);

  T("cp1251 folds e acute because it is not cp1251",
    UC_MODE_CLEAR, UC_ENC_CP1251, 0,
    "\xC3\xA9",
    "e",
    1);

  T("cp1251 deletes C1 control U+0080",
    UC_MODE_CLEAR, UC_ENC_CP1251, 0,
    "\xC2\x80",
    "",
    1);

  /*
   * Typography is normalized even if allowed by cp1251.
   */
  T("cp1251 normalizes LEFT-POINTING DOUBLE ANGLE QUOTATION MARK",
    UC_MODE_CLEAR, UC_ENC_CP1251, 0,
    "\xC2\xAB",
    "\x22",
    1);

  T("cp1251 normalizes EN DASH",
    UC_MODE_CLEAR, UC_ENC_CP1251, 0,
    "\xE2\x80\x93",
    "-",
    1);

  T("cp1251 normalizes HORIZONTAL ELLIPSIS",
    UC_MODE_CLEAR, UC_ENC_CP1251, 0,
    "\xE2\x80\xA6",
    "...",
    1);

  /*
   * Show mode respects encodings.
   */
  T("show cp1252 passes allowed e acute",
    UC_MODE_SHOW, UC_ENC_CP1252, 0,
    "caf"
    "\xC3\xA9",
    "caf"
    "\xC3\xA9",
    0);

  T("show cp1251 passes allowed Cyrillic",
    UC_MODE_SHOW, UC_ENC_CP1251, 0,
    "\xD0\x90",
    "\xD0\x90",
    0);

  /*
   * Highlight mode without color.
   */
  T("highlight ascii passthrough",
    UC_MODE_HIGHLIGHT, UC_ENC_ASCII, 0,
    "abc",
    "abc",
    0);

  T("highlight visible bad char passes through when color disabled",
    UC_MODE_HIGHLIGHT, UC_ENC_ASCII, 0,
    "\xD0\x90",
    "\xD0\x90",
    1);

  T("highlight NBSP becomes U+FFFD when color disabled",
    UC_MODE_HIGHLIGHT, UC_ENC_ASCII, 0,
    "\xC2\xA0",
    "\xEF\xBF\xBD",
    1);

  T("highlight control becomes U+FFFD when color disabled",
    UC_MODE_HIGHLIGHT, UC_ENC_ASCII, 0,
    "\x01",
    "\xEF\xBF\xBD",
    1);

  /*
   * Color output.
   */
  T("color highlight visible bad char",
    UC_MODE_HIGHLIGHT, UC_ENC_ASCII, 1,
    "\xD0\x9F",
    "\x1b[0;31m"
    "\xD0\x9F"
    "\x1b[0m",
    1);

  T("color highlight NBSP",
    UC_MODE_HIGHLIGHT, UC_ENC_ASCII, 1,
    "\xC2\xA0",
    "\x1b[0;31m"
    "\xEF\xBF\xBD"
    "\x1b[0m",
    1);

  T("color show NBSP",
    UC_MODE_SHOW, UC_ENC_ASCII, 1,
    "\xC2\xA0",
    "\x1b[0;31m"
    "[U+00A0]"
    "\x1b[0m",
    1);

  T("color show invalid byte",
    UC_MODE_SHOW, UC_ENC_ASCII, 1,
    "\xFF",
    "\x1b[0;31m"
    "[INVALID]"
    "\x1b[0m",
    1);

  T("color clear unknown char becomes colored ?",
    UC_MODE_CLEAR, UC_ENC_ASCII, 1,
    "\xD0\x90",
    "\x1b[0;31m"
    "?"
    "\x1b[0m",
    1);

  T("color clear NBSP becomes uncolored space",
    UC_MODE_CLEAR, UC_ENC_ASCII, 1,
    "\xC2\xA0",
    " ",
    1);

  T("color clear EN DASH becomes uncolored hyphen",
    UC_MODE_CLEAR, UC_ENC_ASCII, 1,
    "\xE2\x80\x93",
    "-",
    1);

  T("color clear zero width deletes without color output",
    UC_MODE_CLEAR, UC_ENC_ASCII, 1,
    "\xE2\x80\x8B",
    "",
    1);

  /*
   * Multiple issues.
   */
  T("multiple hidden chars",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC2\xA0"
    "\xE2\x80\x8B"
    "\xC2\xA0",
    "  ",
    3);

  T("multiple mixed issues",
    UC_MODE_CLEAR, UC_ENC_ASCII, 0,
    "\xC2\xA0"
    "\xE2\x80\x93"
    "\xE2\x80\x8B"
    "x",
    " -x",
    3);

  /*
   * Full encoding tables.
   */
  check_encoding("cp1252", UC_ENC_CP1252,
                 cp1252_allowed, sizeof(cp1252_allowed) / sizeof(*cp1252_allowed),
                 cp1252_rejected, sizeof(cp1252_rejected) / sizeof(*cp1252_rejected));

  check_encoding("cp1251", UC_ENC_CP1251,
                 cp1251_allowed, sizeof(cp1251_allowed) / sizeof(*cp1251_allowed),
                 cp1251_rejected, sizeof(cp1251_rejected) / sizeof(*cp1251_rejected));

  /*
   * Diacritic folding.
   */
  TF("fold diacritics cafe",
     UC_MODE_CLEAR, UC_ENC_CP1252, 0,
     "caf\xC3\xA9",
     "cafe",
     1);

  T("fold diacritics cafe without flag keeps accent",
     UC_MODE_CLEAR, UC_ENC_CP1252, 0,
     "caf\xC3\xA9",
     "caf\xC3\xA9",
     0);

  TF("fold diacritics e acute",
     UC_MODE_CLEAR, UC_ENC_CP1252, 0,
     "\xC3\xA9",
     "e",
     1);

  TF("fold diacritics E acute",
     UC_MODE_CLEAR, UC_ENC_CP1252, 0,
     "\xC3\x89",
     "E",
     1);

  TF("fold diacritics u diaeresis",
     UC_MODE_CLEAR, UC_ENC_CP1252, 0,
     "\xC3\xBC",
     "u",
     1);

  TF("fold diacritics n tilde",
     UC_MODE_CLEAR, UC_ENC_CP1252, 0,
     "\xC3\xB1",
     "n",
     1);

  TF("fold diacritics a with macron",
     UC_MODE_CLEAR, UC_ENC_CP1252, 0,
     "\xC4\x81",
     "a",
     1);

  TF("fold diacritics O with horn",
     UC_MODE_CLEAR, UC_ENC_CP1252, 0,
     "\xC6\xA0",
     "O",
     1);

  TF("fold diacritics A with dot above and macron",
     UC_MODE_CLEAR, UC_ENC_CP1252, 0,
     "\xC7\xA0",
     "A",
     1);

  TF("fold diacritics shows problem in show mode",
     UC_MODE_SHOW, UC_ENC_CP1252, 0,
     "\xC3\xA9",
     "[U+00E9]",
     1);

  TF("fold diacritics ascii encoding folds too",
     UC_MODE_CLEAR, UC_ENC_ASCII, 0,
     "\xC3\xA9",
     "e",
     1);

  TF("fold diacritics cp1251 folds too",
     UC_MODE_CLEAR, UC_ENC_CP1251, 0,
     "\xC3\xA9",
     "e",
     1);

  check_diacritic_table("diacritic latin", diacritic_latin, 0x00C0,
                        sizeof(diacritic_latin) - 1);

  check_diacritic_table("diacritic extended", diacritic_extended, 0x1E00,
                        sizeof(diacritic_extended) - 1);

  /*
   * Invisible code points.
   */
  check_invisible_ranges("format", format_ranges,
                          sizeof(format_ranges) / sizeof(*format_ranges));

  check_invisible_ranges("combining", combining_ranges,
                          sizeof(combining_ranges) / sizeof(*combining_ranges));

  fprintf(stderr, "%d tests run, %d failed\n", tests_run, tests_failed);
  return tests_failed ? 1 : 0;
}