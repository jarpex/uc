#ifndef UC_H
#define UC_H

#include <stddef.h>

enum uc_mode
{
    UC_MODE_HIGHLIGHT,
    UC_MODE_SHOW,
    UC_MODE_CLEAR
};

enum uc_enc
{
    UC_ENC_ASCII,
    UC_ENC_CP1252,
    UC_ENC_CP1251
};

struct uc_opts
{
    enum uc_mode mode;
    enum uc_enc enc;
    int use_color;
};

/*
 * Write callback contract:
 *
 *   return 0 on success
 *   return negative value on error
 */
typedef int (*uc_write_fn)(const char *buf, size_t len, void *arg);

struct uc_ctx;

struct uc_ctx *uc_new(const struct uc_opts *opts);

/*
 * uc_free(NULL) is safe.
 */
void uc_free(struct uc_ctx *ctx);

int uc_feed(struct uc_ctx *ctx, const void *buf, size_t len,
            uc_write_fn write, void *arg);

int uc_end(struct uc_ctx *ctx, uc_write_fn write, void *arg);

unsigned long uc_issues(const struct uc_ctx *ctx);

#endif