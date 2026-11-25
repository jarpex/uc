#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "uc.h"

enum color_mode {
	COLOR_AUTO,
	COLOR_ALWAYS,
	COLOR_NEVER
};

static void usage(FILE *out) {
	fprintf(out,
		"usage: uc [-h] [-v] [-s] [-c] [-e encoding] [--check]\n"
		"          [--color when] [--fold-diacritics] [--] [text...]\n"
		"\n"
		"options:\n"
		"  -h, --help       show help\n"
		"  -v, --version    show version\n"
		"  -s, --show       show problematic characters as [U+XXXX]\n"
		"  -c, --clear      clean output text\n"
		"  -e, --encoding   target encoding: ascii, cp1252, cp1251\n"
		"      --check      exit code 1 if problems were found\n"
		"      --color when colorize output: auto, always, never\n"
		"      --fold-diacritics\n"
		"                   fold letters with diacritics to ASCII base\n"
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
		"  repair letters that mix scripts inside a word\n"
		"  --fold-diacritics also folds letters with diacritics\n"
		"\n"
		"encodings:\n"
		"  ascii\n"
		"  cp1252\n"
		"  cp1251\n"
		"\n"
		"  -e selects the allowed character set.\n"
		"  Output remains UTF-8. Use iconv if legacy byte output is\n"
		"  needed.\n"
		"\n"
		"input:\n"
		"  If text arguments are given, they are processed as input\n"
		"  strings.\n"
		"  Each text argument is treated as a separate line.\n"
		"  A text argument of '-' reads standard input at that position.\n"
		"  Use '--' to stop option parsing.\n"
		"\n"
		"  If no text arguments are given and standard input is a\n"
		"  terminal, this help message is printed.\n"
		"\n"
		"  If no text arguments are given and standard input is not a\n"
		"  terminal, uc reads standard input.\n"
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

static int32_t parse_encoding(const char *s, enum uc_enc *enc) {
	if (strcmp(s, "ascii") == 0) {
		*enc = UC_ENC_ASCII;
		return 0;
	}

	if (strcmp(s, "cp1252") == 0) {
		*enc = UC_ENC_CP1252;
		return 0;
	}

	if (strcmp(s, "cp1251") == 0) {
		*enc = UC_ENC_CP1251;
		return 0;
	}

	return -1;
}

static int32_t parse_color(const char *s, enum color_mode *color) {
	if (strcmp(s, "auto") == 0) {
		*color = COLOR_AUTO;
		return 0;
	}

	if (strcmp(s, "always") == 0) {
		*color = COLOR_ALWAYS;
		return 0;
	}

	if (strcmp(s, "never") == 0) {
		*color = COLOR_NEVER;
		return 0;
	}

	return -1;
}

static int32_t write_stdout(const char *buf, size_t len, void *arg) {
	(void)arg;

	if (len == 0) {
		return 0;
	}

	if (fwrite(buf, 1, len, stdout) != len) {
		return -1;
	}

	return 0;
}

static int32_t process_stdin(struct uc_ctx *ctx) {
	unsigned char buf[8192];
	size_t n = 0;

	while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
		if (uc_feed(ctx, buf, n, write_stdout, NULL) < 0) {
			return -1;
		}
	}

	if (ferror(stdin)) {
		return -1;
	}

	return 0;
}

static int32_t add_input(const char ***inputs, size_t *len, size_t *cap,
	const char *s) {
	size_t newcap = 0;
	const char **tmp = NULL;

	if (*len == *cap) {
		newcap = (*cap == 0) ? 8 : (*cap << 1);
		tmp = realloc(*inputs, newcap * sizeof(**inputs));
		if (!tmp) {
			return -1;
		}

		*inputs = tmp;
		*cap = newcap;
	}

	(*inputs)[*len] = s;
	(*len)++;
	return 0;
}

struct cli_state {
	struct uc_opts opts;
	enum color_mode color;
	int32_t check;
	int32_t mode_set;
};

/*
 * Returns 1 to continue, 0 for clean exit, -1 on error.
 */
static int32_t parse_option(int32_t argc, char **argv, int32_t *idx,
	struct cli_state *st) {
	const char *a = argv[*idx];

	if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
		usage(stdout);
		return 0;
	}

	if (strcmp(a, "-v") == 0 || strcmp(a, "--version") == 0) {
		printf("uc " UC_VERSION "\n");
		return 0;
	}

	if (strcmp(a, "-s") == 0 || strcmp(a, "--show") == 0) {
		if (st->mode_set && st->opts.mode != UC_MODE_SHOW) {
			fprintf(stderr, "uc: -s and -c are mutually exclusive\n");
			return -1;
		}

		st->opts.mode = UC_MODE_SHOW;
		st->mode_set = 1;
		return 1;
	}

	if (strcmp(a, "-c") == 0 || strcmp(a, "--clear") == 0) {
		if (st->mode_set && st->opts.mode != UC_MODE_CLEAR) {
			fprintf(stderr, "uc: -s and -c are mutually exclusive\n");
			return -1;
		}

		st->opts.mode = UC_MODE_CLEAR;
		st->mode_set = 1;
		return 1;
	}

	if (strcmp(a, "-e") == 0 || strcmp(a, "--encoding") == 0) {
		if (++(*idx) >= argc) {
			fprintf(stderr, "uc: missing encoding argument\n");
			usage(stderr);
			return -1;
		}

		if (parse_encoding(argv[*idx], &st->opts.enc) < 0) {
			fprintf(stderr,
				"uc: unknown encoding: %s "
				"(supported: ascii, cp1252, cp1251)\n",
				argv[*idx]);
			return -1;
		}

		return 1;
	}

	if (strncmp(a, "--encoding=", 11) == 0) {
		if (parse_encoding(a + 11, &st->opts.enc) < 0) {
			fprintf(stderr,
				"uc: unknown encoding: %s "
				"(supported: ascii, cp1252, cp1251)\n",
				a + 11);
			return -1;
		}

		return 1;
	}

	if (strcmp(a, "--check") == 0) {
		st->check = 1;
		return 1;
	}

	if (strcmp(a, "--fold-diacritics") == 0) {
		st->opts.fold_diacritics = 1;
		return 1;
	}

	if (strcmp(a, "--color") == 0) {
		if (++(*idx) >= argc) {
			fprintf(stderr, "uc: missing color argument\n");
			usage(stderr);
			return -1;
		}

		if (parse_color(argv[*idx], &st->color) < 0) {
			fprintf(stderr,
				"uc: unknown color: %s "
				"(supported: auto, always, never)\n",
				argv[*idx]);
			return -1;
		}

		return 1;
	}

	if (strncmp(a, "--color=", 8) == 0) {
		if (parse_color(a + 8, &st->color) < 0) {
			fprintf(stderr,
				"uc: unknown color: %s "
				"(supported: auto, always, never)\n",
				a + 8);
			return -1;
		}

		return 1;
	}

	fprintf(stderr, "uc: unknown option: %s\n", a);
	usage(stderr);
	return -1;
}

static int32_t process_inputs(struct uc_ctx *ctx, const char **inputs,
	size_t inputs_len) {
	if (inputs_len == 0) {
		return process_stdin(ctx);
	}

	for (size_t j = 0; j < inputs_len; j++) {
		if (strcmp(inputs[j], "-") == 0) {
			if (process_stdin(ctx) < 0) {
				return -1;
			}
			continue;
		}

		if (uc_feed(ctx, inputs[j], strlen(inputs[j]),
			write_stdout, NULL) < 0) {
			return -1;
		}

		/*
		 * Each text argument is treated as a separate line.
		 */
		if (uc_feed(ctx, "\n", 1, write_stdout, NULL) < 0) {
			return -1;
		}
	}

	return 0;
}

int main(int argc, char **argv) {
	struct cli_state st = {{0}, COLOR_AUTO, 0, 0};
	struct uc_ctx *ctx = NULL;
	const char **inputs = NULL;
	size_t inputs_len = 0;
	size_t inputs_cap = 0;
	uint64_t issues = 0;
	int32_t no_more_options = 0;
	int32_t rc = 0;
	const char *a = NULL;

	st.opts.mode = UC_MODE_HIGHLIGHT;
	st.opts.enc = UC_ENC_ASCII;

	for (int32_t i = 1; i < argc; i++) {
		a = argv[i];

		if (no_more_options || a[0] != '-' || a[1] == '\0') {
			if (add_input(&inputs, &inputs_len, &inputs_cap, a) < 0) {
				fprintf(stderr, "uc: out of memory\n");
				free(inputs);
				return 2;
			}
			continue;
		}

		if (strcmp(a, "--") == 0) {
			no_more_options = 1;
			continue;
		}

		rc = parse_option((int32_t)argc, argv, &i, &st);
		if (rc <= 0) {
			free(inputs);
			return (rc == 0) ? 0 : 2;
		}
	}

	/*
	 * If there is no input text and stdin is a terminal, show help.
	 */
	if (inputs_len == 0 && isatty(fileno(stdin))) {
		usage(stdout);
		free(inputs);
		return 0;
	}

	switch (st.color) {
	case COLOR_ALWAYS:
		st.opts.use_color = 1;
		break;

	case COLOR_NEVER:
		st.opts.use_color = 0;
		break;

	case COLOR_AUTO:
	default:
		st.opts.use_color = isatty(fileno(stdout))
			&& getenv("NO_COLOR") == NULL;
		break;
	}

	ctx = uc_new(&st.opts);
	if (!ctx) {
		fprintf(stderr, "uc: out of memory\n");
		free(inputs);
		return 2;
	}

	if (process_inputs(ctx, inputs, inputs_len) < 0) {
		uc_free(ctx);
		free(inputs);
		return 2;
	}

	if (uc_end(ctx, write_stdout, NULL) < 0) {
		uc_free(ctx);
		free(inputs);
		return 2;
	}

	if (fflush(stdout) != 0) {
		uc_free(ctx);
		free(inputs);
		return 2;
	}

	issues = uc_issues(ctx);

	uc_free(ctx);
	free(inputs);

	if (st.check && issues > 0) {
		return 1;
	}

	return 0;
}
