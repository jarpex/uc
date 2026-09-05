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
                       const char *in, size_t inlen,
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

  if (run_uc_full(mode, enc, color, in, inlen,
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
  check_case(name, mode, enc, color, input, sizeof(input) - 1, \
             expected, sizeof(expected) - 1, issues)

#define T2(name, mode, enc, chunk1, chunk2, expected, issues)   \
  check_two_chunks(name, mode, enc, chunk1, sizeof(chunk1) - 1, \
                   chunk2, sizeof(chunk2) - 1,                  \
                   expected, sizeof(expected) - 1, issues)

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

  fprintf(stderr, "%d tests run, %d failed\n", tests_run, tests_failed);
  return tests_failed ? 1 : 0;
}