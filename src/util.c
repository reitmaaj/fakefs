#include "fakefs_internal.h"

#include <errno.h>

static void txn_rollback(ffs *fs, int r, int *done)
{
    rollback(fs);
    *done = r;
}

static void txn_commit(ffs *fs, int *done)
{
    *done = commit(fs);
}

static void txn_settle(ffs *fs, int r, int *done)
{
    if (r != 0)
    {
        txn_rollback(fs, r, done);
        return;
    }
    txn_commit(fs, done);
}

static int txn_body(ffs *fs, int (*op)(ffs *fs, void *ctx), void *ctx)
{
    int r;

    r = op(fs, ctx);
    txn_settle(fs, r, &r);
    return r;
}

static int txn_guard(ffs *fs, int (*op)(ffs *fs, void *ctx), void *ctx, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = txn_body(fs, op, ctx);
    return r;
}

int txn_run(ffs *fs, int (*op)(ffs *fs, void *ctx), void *ctx)
{
    int r;

    r = begin(fs);
    r = txn_guard(fs, op, ctx, r);
    return r;
}

int util_fail(int e)
{
    errno = e;
    return -1;
}

int util_fail_r(int r)
{
    errno = -r;
    return -1;
}

int fail_code(int r)
{
    if (r < 0)
    {
        r = util_fail_r(r);
        return r;
    }
    return r;
}

int either_null(const void *a, const void *b)
{
    if (a == NULL)
    {
        return 1;
    }
    return b == NULL;
}

int any_null3(const void *a, const void *b, const void *c)
{
    int ab;

    ab = either_null(a, b);
    return choose(ab, 1, c == NULL);
}

int neg_code(long v)
{
    if (v < 0)
    {
        return (int)v;
    }
    return 0;
}

int fd_range_ok(int fd)
{
    if (fd < 0)
    {
        return 0;
    }
    return fd < FFS_MAX_FD;
}

int fd_valid(const ffs *fs, int fd)
{
    if (fd_range_ok(fd) == 0)
    {
        return 0;
    }
    return fs->fds[fd].used != 0;
}

int fd_bad(const ffs *fs, int fd)
{
    if (fs == NULL)
    {
        return 1;
    }
    return fd_valid(fs, fd) == 0;
}

int fd_of(ffs *fs, int fd, int *of)
{
    if (fd_bad(fs, fd))
    {
        return -EBADF;
    }
    *of = fs->fds[fd].of;
    return 0;
}

static long q_value(sqlite3_stmt *s, int rc, long err)
{
    if (s == NULL)
    {
        return -EIO; /* LCOV_EXCL_LINE */
    }
    if (rc == SQLITE_ROW)
    {
        err = sql_col_i64(s, 0);
    }
    return err;
}

long q_i64(sqlite3_stmt *s, long err)
{
    int rc;
    long v;

    rc = sqlite3_step(s);
    v = q_value(s, rc, err);
    sqlite3_finalize(s);
    return v;
}

void sql_bind_i64(sqlite3_stmt *s, int i, long v)
{
    sqlite3_bind_int64(s, i, v);
}

void sql_bind_text(sqlite3_stmt *s, int i, const char *v)
{
    sqlite3_bind_text(s, i, v, -1, SQLITE_TRANSIENT);
}

int sql_row(sqlite3_stmt *s)
{
    int rc;
    int result;

    rc = sqlite3_step(s);
    result = rc == SQLITE_ROW;
    return result;
}

long sql_col_i64(sqlite3_stmt *s, int col)
{
    long v;

    v = (long)sqlite3_column_int64(s, col);
    return v;
}

void sql_fin(sqlite3_stmt *s)
{
    sqlite3_finalize(s);
}

int done_result(int rc)
{
    if (rc == SQLITE_DONE)
    {
        return 0;
    }
    return -EIO;
}

int sql_done(sqlite3_stmt *s)
{
    int rc;
    int result;

    rc = sqlite3_step(s);
    sqlite3_finalize(s);
    result = done_result(rc);
    return result;
}
