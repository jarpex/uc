#ifndef UC_H
#define UC_H

#include <stddef.h>
#include <stdint.h>

#define UC_VERSION "1.0.2"

enum uc_mode {
	UC_MODE_HIGHLIGHT,
	UC_MODE_SHOW,
	UC_MODE_CLEAR
};

enum uc_enc {
	UC_ENC_ASCII,
	UC_ENC_CP1252,
	UC_ENC_CP1251
};

struct uc_opts {
	enum uc_mode mode;
	enum uc_enc enc;
	int32_t use_color;
	int32_t fold_diacritics;
};

/*
 * Write callback contract:
 *
 *   return 0 on success
 *   return negative value on error
 */
typedef int32_t (*uc_write_fn)(const char *buf, size_t len, void *arg);

struct uc_ctx;

struct uc_ctx *uc_new(const struct uc_opts *opts);

/*
 * uc_free(NULL) is safe.
 */
void uc_free(struct uc_ctx *ctx);

int32_t uc_feed(struct uc_ctx *ctx, const void *buf, size_t len,
	uc_write_fn write, void *arg);

int32_t uc_end(struct uc_ctx *ctx, uc_write_fn write, void *arg);

uint64_t uc_issues(const struct uc_ctx *ctx);

#endif
