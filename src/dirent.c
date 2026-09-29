#include "fakefs_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define SQL_DIR_LOOKUP "SELECT ino FROM dirents WHERE parent = ? AND name = ?"
#define SQL_DIR_INSERT "INSERT INTO dirents (parent,name,ino) VALUES (?,?,?)"
#define SQL_DIR_DELETE "DELETE FROM dirents WHERE parent = ? AND name = ?"
#define SQL_DIR_ANY "SELECT parent, name FROM dirents WHERE ino = ? LIMIT 1"
#define SQL_DIR_TOUCH "UPDATE inodes SET mtime = ?, ctime = ? WHERE ino = ?"
#define SQL_DIR_NLINK "UPDATE inodes SET nlink = nlink + ? WHERE ino = ?"
#define SQL_DIR_COUNT "SELECT COUNT(*) FROM dirents WHERE parent = ?"
#define SQL_DIR_NAME                                                           \
    "SELECT name FROM dirents WHERE parent = ? AND ino = ? LIMIT 1"

int insert_code(int rc)
{
    if (rc == SQLITE_CONSTRAINT)
    {
        return -EEXIST;
    }
    if (rc == SQLITE_DONE)
    {
        return 0;
    }
    return -EIO;
}

static void bind2(sqlite3_stmt *s, long a, long b)
{
    sql_bind_i64(s, 1, a);
    sql_bind_i64(s, 2, b);
}

static void bind3(sqlite3_stmt *s, long a, long b, long c)
{
    bind2(s, a, b);
    sql_bind_i64(s, 3, c);
}

static void bind_pt(sqlite3_stmt *s, long a, const char *b)
{
    sql_bind_i64(s, 1, a);
    sql_bind_text(s, 2, b);
}

static void bind_pt3(sqlite3_stmt *s, long a, const char *b, long c)
{
    bind_pt(s, a, b);
    sql_bind_i64(s, 3, c);
}

static char *row_text_col(sqlite3_stmt *s, int col)
{
    const unsigned char *txt;
    char *dup;

    txt = sqlite3_column_text(s, col);
    dup = xstrdup((const char *)txt);
    return dup;
}

static char *row_text(sqlite3_stmt *s)
{
    char *r;
    int isrow;

    isrow = sql_row(s);
    r = NULL;
    if (isrow)
    {
        r = row_text_col(s, 0);
    }
    sql_fin(s);
    return r;
}

static void read_dir_row(sqlite3_stmt *s, long *parent, char **name)
{
    *parent = sql_col_i64(s, 0);
    *name = row_text_col(s, 1);
}

static void read_if_row(sqlite3_stmt *s, int isrow, long *parent, char **name)
{
    if (isrow)
    {
        read_dir_row(s, parent, name);
    }
}

static int any_out(char **name, char *r)
{
    if (r == NULL)
    {
        return -ENOENT; /* LCOV_EXCL_LINE */
    }
    *name = r;
    return 0;
}

static int row_any(sqlite3_stmt *s, long *parent, char **name)
{
    char *r;
    int isrow;
    int rc;

    r = NULL;
    isrow = sql_row(s);
    read_if_row(s, isrow, parent, &r);
    sql_fin(s);
    rc = any_out(name, r);
    return rc;
}

static long dir_i64(ffs *fs, const char *sql, long a, const char *b, long err)
{
    sqlite3_stmt *s;
    long v;

    s = prepare(fs, sql);
    bind_pt(s, a, b);
    v = q_i64(s, err);
    return v;
}

static long dir_i64_1(ffs *fs, const char *sql, long a, long err)
{
    sqlite3_stmt *s;
    long v;

    s = prepare(fs, sql);
    sql_bind_i64(s, 1, a);
    v = q_i64(s, err);
    return v;
}

static char *dir_text(ffs *fs, const char *sql, long a, long b)
{
    sqlite3_stmt *s;
    char *r;

    s = prepare(fs, sql);
    bind2(s, a, b);
    r = row_text(s);
    return r;
}

static void dir_exec2(ffs *fs, const char *sql, long a, long b, int *out)
{
    sqlite3_stmt *s;

    s = prepare(fs, sql);
    bind2(s, a, b);
    *out = sql_done(s);
}

static void dir_exec3(ffs *fs, const char *sql, long a, long b, long c,
                      int *out)
{
    sqlite3_stmt *s;

    s = prepare(fs, sql);
    bind3(s, a, b, c);
    *out = sql_done(s);
}

static void dir_exec_pt(ffs *fs, const char *sql, long a, const char *b,
                        int *out)
{
    sqlite3_stmt *s;

    s = prepare(fs, sql);
    bind_pt(s, a, b);
    *out = sql_done(s);
}

static int dir_insert_code(sqlite3_stmt *s)
{
    int rc;
    int r;

    rc = sqlite3_step(s);
    sql_fin(s);
    r = insert_code(rc);
    return r;
}

static void dir_insert_run(ffs *fs, long parent, const char *name, long ino,
                           int *out)
{
    sqlite3_stmt *s;

    s = prepare(fs, SQL_DIR_INSERT);
    bind_pt3(s, parent, name, ino);
    *out = dir_insert_code(s);
}

long dirent_lookup(ffs *fs, long parent, const char *name)
{
    long v;

    v = dir_i64(fs, SQL_DIR_LOOKUP, parent, name, -ENOENT);
    return v;
}

int dirent_insert(ffs *fs, long parent, const char *name, long ino)
{
    int r;

    dir_insert_run(fs, parent, name, ino, &r);
    return r;
}

int dirent_delete(ffs *fs, long parent, const char *name)
{
    int r;

    dir_exec_pt(fs, SQL_DIR_DELETE, parent, name, &r);
    return r;
}

int dirent_any(ffs *fs, long ino, long *parent, char **name)
{
    sqlite3_stmt *s;
    int r;

    s = prepare(fs, SQL_DIR_ANY);
    if (s == NULL)
    {
        return -EIO; /* LCOV_EXCL_LINE */
    }
    sql_bind_i64(s, 1, ino);
    r = row_any(s, parent, name);
    return r;
}

int dir_touch(ffs *fs, long ino, long t)
{
    int r;

    dir_exec3(fs, SQL_DIR_TOUCH, t, t, ino, &r);
    return r;
}

int dir_nlink(ffs *fs, long ino, long delta)
{
    int r;

    dir_exec2(fs, SQL_DIR_NLINK, delta, ino, &r);
    return r;
}

int dir_count(ffs *fs, long ino)
{
    long v;

    v = dir_i64_1(fs, SQL_DIR_COUNT, ino, -1);
    return (int)v;
}

char *dirent_name_of(ffs *fs, long parent, long ino)
{
    char *r;

    r = dir_text(fs, SQL_DIR_NAME, parent, ino);
    return r;
}

int is_dir(ffs *fs, long ino)
{
    long mode;
    int r;

    r = inode_mode(fs, ino, &mode);
    if (r < 0)
    {
        return 0; /* LCOV_EXCL_LINE */
    }
    return mode_is_dir(mode);
}

long parent_of(ffs *fs, long ino)
{
    long p;
    int r;

    r = inode_parent(fs, ino, &p);
    if (r < 0)
    {
        return -ENOENT;
    }
    return p;
}
