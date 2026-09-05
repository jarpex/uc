#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "uc.h"

static void usage(FILE *out)
{
    fprintf(out,
            "usage: uc [-h] [-v] [-s] [-c] [-e encoding] [--check] [--] [text...]\n"
            "\n"
            "options:\n"
            "  -h, --help       show help\n"
            "  -v, --version    show version\n"
            "  -s, --show       show problematic characters as [U+XXXX]\n"
            "  -c, --clear      clean output text\n"
            "  -e, --encoding   target encoding: ascii, cp1252, cp1251\n"
            "      --check      exit code 1 if problems were found\n"
            "\n"
            "modes:\n"
            "  default          highlight problematic characters\n"
            "  -s, --show       show problematic characters as [U+XXXX]\n"
            "  -c, --clear      clean output text\n"
            "\n"
            "highlight mode:\n"
            "  Visible problematic characters are highlighted with ANSI color\n"
            "  when stdout is a terminal and NO_COLOR is not set.\n"
            "  Invisible and control characters are shown as U+FFFD.\n"
            "\n"
            "clear policy:\n"
            "  preserve \\n, \\r, \\t and ASCII printable\n"
            "  delete controls and invisible/format characters\n"
            "  normalize Unicode spaces to ASCII space\n"
            "  normalize typography to ASCII\n"
            "  fold characters outside target encoding where possible\n"
            "  replace other unmapped bad characters with '?'\n"
            "\n"
            "encodings:\n"
            "  ascii\n"
            "  cp1252\n"
            "  cp1251\n"
            "\n"
            "  -e selects the allowed character set.\n"
            "  Output remains UTF-8. Use iconv if legacy byte output is needed.\n"
            "\n"
            "input:\n"
            "  If text arguments are given, they are processed as input strings.\n"
            "  Each text argument is treated as a separate line.\n"
            "  A text argument of '-' reads standard input at that position.\n"
            "  Use '--' to stop option parsing.\n"
            "\n"
            "  If no text arguments are given and standard input is a terminal,\n"
            "  this help message is printed.\n"
            "\n"
            "  If no text arguments are given and standard input is not a terminal,\n"
            "  uc reads standard input.\n"
            "\n"
            "exit codes:\n"
            "  0  success\n"
            "  1  problems found and --check was given\n"
            "  2  usage or I/O error\n"
            "\n"
            "examples:\n"
            "  uc \"text\"\n"
            "  uc -s \"text\"\n"
            "  uc -c -e ascii < input.txt > output.txt\n"
            "  uc --check -e cp1251 < input.txt\n");
}

static int parse_encoding(const char *s, enum uc_enc *enc)
{
    if (strcmp(s, "ascii") == 0)
    {
        *enc = UC_ENC_ASCII;
        return 0;
    }

    if (strcmp(s, "cp1252") == 0)
    {
        *enc = UC_ENC_CP1252;
        return 0;
    }

    if (strcmp(s, "cp1251") == 0)
    {
        *enc = UC_ENC_CP1251;
        return 0;
    }

    return -1;
}

static int write_stdout(const char *buf, size_t len, void *arg)
{
    (void)arg;

    if (len == 0)
        return 0;

    if (fwrite(buf, 1, len, stdout) != len)
        return -1;

    return 0;
}

static int process_stdin(struct uc_ctx *ctx)
{
    unsigned char buf[8192];
    size_t n;

    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0)
    {
        if (uc_feed(ctx, buf, n, write_stdout, NULL) < 0)
            return -1;
    }

    if (ferror(stdin))
        return -1;

    return 0;
}

static int add_input(const char ***inputs, size_t *len, size_t *cap,
                     const char *s)
{
    if (*len == *cap)
    {
        size_t newcap;
        const char **tmp;

        newcap = (*cap == 0) ? 8 : *cap * 2;
        tmp = realloc(*inputs, newcap * sizeof(**inputs));
        if (!tmp)
            return -1;

        *inputs = tmp;
        *cap = newcap;
    }

    (*inputs)[*len] = s;
    (*len)++;
    return 0;
}

int main(int argc, char **argv)
{
    struct uc_opts opts;
    struct uc_ctx *ctx = NULL;
    const char **inputs = NULL;
    size_t inputs_len = 0;
    size_t inputs_cap = 0;

    unsigned long issues = 0;

    int check = 0;
    int mode_set = 0;
    int no_more_options = 0;
    int i;

    memset(&opts, 0, sizeof(opts));
    opts.mode = UC_MODE_HIGHLIGHT;
    opts.enc = UC_ENC_ASCII;

    for (i = 1; i < argc; i++)
    {
        const char *a = argv[i];

        if (no_more_options)
        {
            if (add_input(&inputs, &inputs_len, &inputs_cap, a) < 0)
            {
                fprintf(stderr, "uc: out of memory\n");
                free(inputs);
                return 2;
            }
            continue;
        }

        if (strcmp(a, "--") == 0)
        {
            no_more_options = 1;
            continue;
        }

        /*
         * "-" alone means stdin input.
         */
        if (a[0] == '-' && a[1] != '\0')
        {
            if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0)
            {
                usage(stdout);
                free(inputs);
                return 0;
            }

            if (strcmp(a, "-v") == 0 || strcmp(a, "--version") == 0)
            {
                printf("uc 0.1\n");
                free(inputs);
                return 0;
            }

            if (strcmp(a, "-s") == 0 || strcmp(a, "--show") == 0)
            {
                if (mode_set && opts.mode != UC_MODE_SHOW)
                {
                    fprintf(stderr, "uc: -s and -c are mutually exclusive\n");
                    free(inputs);
                    return 2;
                }

                opts.mode = UC_MODE_SHOW;
                mode_set = 1;
                continue;
            }

            if (strcmp(a, "-c") == 0 || strcmp(a, "--clear") == 0)
            {
                if (mode_set && opts.mode != UC_MODE_CLEAR)
                {
                    fprintf(stderr, "uc: -s and -c are mutually exclusive\n");
                    free(inputs);
                    return 2;
                }

                opts.mode = UC_MODE_CLEAR;
                mode_set = 1;
                continue;
            }

            if (strcmp(a, "-e") == 0 || strcmp(a, "--encoding") == 0)
            {
                if (++i >= argc)
                {
                    fprintf(stderr, "uc: missing encoding argument\n");
                    usage(stderr);
                    free(inputs);
                    return 2;
                }

                if (parse_encoding(argv[i], &opts.enc) < 0)
                {
                    fprintf(stderr,
                            "uc: unknown encoding: %s "
                            "(supported: ascii, cp1252, cp1251)\n",
                            argv[i]);
                    free(inputs);
                    return 2;
                }

                continue;
            }

            if (strncmp(a, "--encoding=", 11) == 0)
            {
                if (parse_encoding(a + 11, &opts.enc) < 0)
                {
                    fprintf(stderr,
                            "uc: unknown encoding: %s "
                            "(supported: ascii, cp1252, cp1251)\n",
                            a + 11);
                    free(inputs);
                    return 2;
                }

                continue;
            }

            if (strcmp(a, "--check") == 0)
            {
                check = 1;
                continue;
            }

            fprintf(stderr, "uc: unknown option: %s\n", a);
            usage(stderr);
            free(inputs);
            return 2;
        }

        if (add_input(&inputs, &inputs_len, &inputs_cap, a) < 0)
        {
            fprintf(stderr, "uc: out of memory\n");
            free(inputs);
            return 2;
        }
    }

    /*
     * If there is no input text and stdin is a terminal, show help.
     */
    if (inputs_len == 0 && isatty(fileno(stdin)))
    {
        usage(stdout);
        free(inputs);
        return 0;
    }

    opts.use_color = isatty(fileno(stdout)) && getenv("NO_COLOR") == NULL;

    ctx = uc_new(&opts);
    if (!ctx)
    {
        fprintf(stderr, "uc: out of memory\n");
        free(inputs);
        return 2;
    }

    if (inputs_len == 0)
    {
        if (process_stdin(ctx) < 0)
            goto fail;
    }
    else
    {
        size_t j;

        for (j = 0; j < inputs_len; j++)
        {
            if (strcmp(inputs[j], "-") == 0)
            {
                if (process_stdin(ctx) < 0)
                    goto fail;
            }
            else
            {
                if (uc_feed(ctx, inputs[j], strlen(inputs[j]),
                            write_stdout, NULL) < 0)
                    goto fail;

                /*
                 * Each text argument is treated as a separate line.
                 */
                if (uc_feed(ctx, "\n", 1, write_stdout, NULL) < 0)
                    goto fail;
            }
        }
    }

    if (uc_end(ctx, write_stdout, NULL) < 0)
        goto fail;

    if (fflush(stdout) != 0)
        goto fail;

    issues = uc_issues(ctx);

    uc_free(ctx);
    free(inputs);

    if (check && issues > 0)
        return 1;

    return 0;

fail:
    uc_free(ctx);
    free(inputs);
    return 2;
}