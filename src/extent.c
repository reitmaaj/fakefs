#include "fakefs_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define SQL_EXT_AT                                                             \
    "SELECT off, len, data FROM file_extents"                                  \
    " WHERE ino=? AND off <= ? AND off + len > ?"
#define SQL_EXT_NEXT                                                           \
    "SELECT off, len, data FROM file_extents"                                  \
    " WHERE ino=? AND off + len > ? ORDER BY off LIMIT 1"
#define SQL_EXT_PUT                                                            \
    "INSERT INTO file_extents(ino,off,len,data) VALUES(?,?,?,?)"               \
    " ON CONFLICT(ino,off) DO UPDATE SET len=excluded.len,"                    \
    " data=excluded.data"
#define SQL_EXT_DEL_OVERLAP                                                    \
    "DELETE FROM file_extents WHERE ino=? AND off < ? AND off + len > ?"
#define SQL_EXT_DEL_FROM                                                       \
    "DELETE FROM file_extents WHERE ino=? AND off + len > ?"
#define SQL_EXT_DEL_ALL "DELETE FROM file_extents WHERE ino=?"

int extent_len_bad(long len)
{
    if (len < 0)
    {
        return 1;
    }
    return len > FFS_FILE_PAGE_SIZE;
}

int extent_put_args_bad(ffs_off off, long len)
{
    if (off < 0)
    {
        return 1;
    }
    if (len < 1)
    {
        return 1;
    }
    return len > FFS_FILE_PAGE_SIZE;
}

static void ext_args1(ffs_off *args, ffs_off a)
{
    args[0] = a;
}

static void ext_args2(ffs_off *args, ffs_off a, ffs_off b)
{
    args[0] = a;
    args[1] = b;
}

static void ext_args3(ffs_off *args, ffs_off a, ffs_off b, ffs_off c)
{
    args[0] = a;
    args[1] = b;
    args[2] = c;
}

static void extent_store_row(sqlite3_stmt *s, ffs_off *eoff, long *elen,
                             unsigned char *buf, long len)
{
    const void *blob;

    *eoff = sql_col_i64(s, 0);
    *elen = len;
    blob = sqlite3_column_blob(s, 2);
    memcpy(buf, blob, (size_t)len);
}

static int extent_copy_row(sqlite3_stmt *s, ffs_off *eoff, long *elen,
                           unsigned char *buf)
{
    long len;

    len = sql_col_i64(s, 1);
    if (extent_len_bad(len))
    {
        sql_fin(s);
        return -EIO; /* LCOV_EXCL_LINE */
    }
    extent_store_row(s, eoff, elen, buf, len);
    sql_fin(s);
    return 1;
}

static void ext_bind_args(sqlite3_stmt *s, const ffs_off *args, int nargs)
{
    int i;

    for (i = 0; i < nargs; ++i)
    {
        sqlite3_bind_int64(s, i + 1, args[i]);
    }
}

static void ext_bind_blob(sqlite3_stmt *s, int i, const unsigned char *blob,
                          long len)
{
    sqlite3_bind_blob(s, i, blob, (int)len, SQLITE_TRANSIENT);
}

static void ext_bind_blob_if(sqlite3_stmt *s, int i, const unsigned char *blob,
                             long len)
{
    if (blob == NULL)
    {
        return;
    }
    ext_bind_blob(s, i, blob, len);
}

static void ext_bind(sqlite3_stmt *s, const ffs_off *args, int nargs,
                     const unsigned char *blob)
{
    ext_bind_args(s, args, nargs);
    ext_bind_blob_if(s, nargs + 1, blob, args[nargs - 1]);
}

static void ext_exec(ffs *fs, const char *sql, const ffs_off *args, int nargs,
                     const unsigned char *blob, int *out)
{
    sqlite3_stmt *s;

    s = prepare(fs, sql);
    ext_bind(s, args, nargs, blob);
    *out = sql_done(s);
}

static int ext_read_row(sqlite3_stmt *s, ffs_off *eoff, long *elen,
                        unsigned char *buf)
{
    int r;

    r = sql_row(s);
    if (r == 0)
    {
        sql_fin(s);
        return 0;
    }
    r = extent_copy_row(s, eoff, elen, buf);
    return r;
}

static int ext_query(ffs *fs, const char *sql, const ffs_off *args, int nargs,
                     ffs_off *eoff, long *elen, unsigned char *buf)
{
    sqlite3_stmt *s;
    int r;

    s = prepare(fs, sql);
    if (s == NULL)
    {
        return -EIO; /* LCOV_EXCL_LINE */
    }
    ext_bind(s, args, nargs, NULL);
    r = ext_read_row(s, eoff, elen, buf);
    return r;
}

int extent_at(ffs *fs, long ino, ffs_off off, ffs_off *eoff, long *elen,
              unsigned char *buf)
{
    ffs_off args[3];
    int r;

    ext_args3(args, ino, off, off);
    r = ext_query(fs, SQL_EXT_AT, args, 3, eoff, elen, buf);
    return r;
}

int extent_next(ffs *fs, long ino, ffs_off off, ffs_off *eoff, long *elen,
                unsigned char *buf)
{
    ffs_off args[2];
    int r;

    ext_args2(args, ino, off);
    r = ext_query(fs, SQL_EXT_NEXT, args, 2, eoff, elen, buf);
    return r;
}

int extent_put(ffs *fs, long ino, ffs_off off, const unsigned char *buf,
               long len)
{
    ffs_off args[3];
    int r;

    if (extent_put_args_bad(off, len))
    {
        return -EINVAL;
    }
    ext_args3(args, ino, off, len);
    ext_exec(fs, SQL_EXT_PUT, args, 3, buf, &r);
    return r;
}

int extent_delete_overlap(ffs *fs, long ino, ffs_off start, ffs_off end)
{
    ffs_off args[3];
    int r;

    ext_args3(args, ino, end, start);
    ext_exec(fs, SQL_EXT_DEL_OVERLAP, args, 3, NULL, &r);
    return r;
}

int extent_delete_from(ffs *fs, long ino, ffs_off start)
{
    ffs_off args[2];
    int r;

    ext_args2(args, ino, start);
    ext_exec(fs, SQL_EXT_DEL_FROM, args, 2, NULL, &r);
    return r;
}

int extent_delete_all(ffs *fs, long ino)
{
    ffs_off args[1];
    int r;

    ext_args1(args, ino);
    ext_exec(fs, SQL_EXT_DEL_ALL, args, 1, NULL, &r);
    return r;
}
