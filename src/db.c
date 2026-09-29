#include "fakefs_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define SQL_ROOT_COUNT "SELECT COUNT(*) FROM inodes WHERE ino = 1"
#define SQL_ROOT_INSERT                                                        \
    "INSERT INTO inodes "                                                      \
    "(ino,parent,mode,uid,gid,nlink,size,atime,mtime,ctime)"                   \
    " VALUES (1,1,?,0,0,1,0,?,?,?)"

sqlite3_stmt *prepare(ffs *fs, const char *sql)
{
    sqlite3_stmt *s;

    s = NULL;
    sqlite3_prepare_v2(fs->db, sql, -1, &s, NULL);
    return s;
}

int exec_code(int rc)
{
    if (rc == SQLITE_OK)
    {
        return 0;
    }
    return -EIO;
}

static int exec_nested(ffs *fs, const char *sql)
{
    int rc;

    rc = sqlite3_exec(fs->db, sql, NULL, NULL, NULL);
    return exec_code(rc);
}

const char *begin_sql(int nested)
{
    if (nested)
    {
        return FFS_SQL_SAVEPOINT;
    }
    return FFS_SQL_BEGIN;
}

const char *commit_sql(int nested)
{
    if (nested)
    {
        return FFS_SQL_RELEASE;
    }
    return FFS_SQL_COMMIT;
}

int depth_down(int depth)
{
    if (depth > 0)
    {
        return depth - 1;
    }
    return depth;
}

int begin(ffs *fs)
{
    int nested;
    int r;

    nested = fs->txn_depth > 0;
    ++fs->txn_depth;
    r = exec_nested(fs, begin_sql(nested));
    return r;
}

int commit(ffs *fs)
{
    int nested;
    int r;

    nested = fs->txn_depth > 1;
    fs->txn_depth = depth_down(fs->txn_depth);
    r = exec_nested(fs, commit_sql(nested));
    return r;
}

static int rb_nested(ffs *fs) /* LCOV_EXCL_LINE */
{
    exec_nested(fs, FFS_SQL_SAVEPOINT_ROLLBACK); /* LCOV_EXCL_LINE */
    exec_nested(fs, FFS_SQL_RELEASE);            /* LCOV_EXCL_LINE */
    return -EIO;                                 /* LCOV_EXCL_LINE */
}

static int rb_outer(ffs *fs) /* LCOV_EXCL_LINE */
{
    exec_nested(fs, FFS_SQL_ROLLBACK); /* LCOV_EXCL_LINE */
    return -EIO;                       /* LCOV_EXCL_LINE */
}

static void rollback_run(ffs *fs, int nested, int *out) /* LCOV_EXCL_LINE */
{
    if (nested)               /* LCOV_EXCL_LINE */
    {                         /* LCOV_EXCL_LINE */
        *out = rb_nested(fs); /* LCOV_EXCL_LINE */
        return;               /* LCOV_EXCL_LINE */
    } /* LCOV_EXCL_LINE */
    *out = rb_outer(fs); /* LCOV_EXCL_LINE */
}

int rollback(ffs *fs)
{
    int nested;
    int r;

    nested = fs->txn_depth > 1;
    fs->txn_depth = depth_down(fs->txn_depth);
    rollback_run(fs, nested, &r);
    return r;
}

long now(void)
{
    time_t t;

    t = time(NULL);
    return t;
}

static void copy_if_ptr(char *p, const char *s, size_t n)
{
    if (p == NULL)
    {
        return;
    }
    memcpy(p, s, n);
}

static char *dup_copy(const char *s, size_t n)
{
    char *p;

    p = (char *)malloc(n);
    copy_if_ptr(p, s, n);
    return p;
}

char *xstrdup(const char *s)
{
    char *p;

    p = NULL;
    if (s != NULL)
    {
        p = dup_copy(s, strlen(s) + 1);
    }
    return p;
}

static void schema_second(ffs *fs, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = exec_nested(fs, SCHEMA_DIRENTS);
}

static int schema_base(ffs *fs)
{
    int r;

    r = exec_nested(fs, SCHEMA_INODES);
    schema_second(fs, &r);
    return r;
}

static int root_count(ffs *fs)
{
    sqlite3_stmt *s;
    long v;

    s = prepare(fs, SQL_ROOT_COUNT);
    v = q_i64(s, -EIO);
    return (int)v;
}

static void bind_root(sqlite3_stmt *s, long t)
{
    sqlite3_bind_int64(s, 1, FFS_ROOT_MODE);
    sqlite3_bind_int64(s, 2, t);
    sqlite3_bind_int64(s, 3, t);
    sqlite3_bind_int64(s, 4, t);
}

static int root_insert(ffs *fs, long t)
{
    sqlite3_stmt *s;
    int r;

    s = prepare(fs, SQL_ROOT_INSERT);
    bind_root(s, t);
    r = sql_done(s);
    return r;
}

static int root_insert_now(ffs *fs)
{
    long t;
    int r;

    t = now();
    r = root_insert(fs, t);
    return r;
}

int root_code(int r)
{
    if (r > 0)
    {
        return 0;
    }
    return r;
}

static void root_insert_if_missing(ffs *fs, int *r)
{
    if (*r != 0)
    {
        *r = root_code(*r);
        return;
    }
    *r = root_insert_now(fs);
}

static int root_ensure(ffs *fs)
{
    int r;

    r = root_count(fs);
    root_insert_if_missing(fs, &r);
    return r;
}

static void root_defaults(ffs *fs)
{
    fs->root = FFS_ROOT_INO;
    fs->cwd = FFS_ROOT_INO;
}

static void schema_root_if(ffs *fs, int *r)
{
    if (*r < 0)
    {
        return;
    }
    root_defaults(fs);
}

static int schema_root(ffs *fs)
{
    int r;

    r = root_ensure(fs);
    schema_root_if(fs, &r);
    return r;
}

static void init_root(ffs *fs, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = schema_root(fs);
}

int init_schema(ffs *fs)
{
    int r;

    r = schema_base(fs);
    init_root(fs, &r);
    return r;
}

static void fs_init(ffs *fs, sqlite3 *db)
{
    fs->db = db;
    fs->umask = FFS_DEFAULT_UMASK;
}

static void fs_init_if(ffs *fs, sqlite3 *db)
{
    if (fs == NULL)
    {
        return;
    }
    fs_init(fs, db);
}

static ffs *fs_new(sqlite3 *db)
{
    ffs *fs;

    fs = (ffs *)calloc(1, sizeof(ffs));
    fs_init_if(fs, db);
    return fs;
}

static ffs *fs_free(ffs *fs) /* LCOV_EXCL_LINE */
{
    free(fs);    /* LCOV_EXCL_LINE */
    return NULL; /* LCOV_EXCL_LINE */
}

static void ready_if(ffs *fs, int r, ffs **out)
{
    if (r >= 0)
    {
        *out = fs;
        return;
    }
    *out = fs_free(fs); /* LCOV_EXCL_LINE */
}

static ffs *fs_ready(ffs *fs)
{
    ffs *p;
    int r;

    r = init_schema(fs);
    ready_if(fs, r, &p);
    return p;
}

static void alloc_ready(ffs *fs, ffs **out)
{
    if (fs == NULL)
    {
        *out = NULL; /* LCOV_EXCL_LINE */
        return;
    }
    *out = fs_ready(fs);
}

ffs *ffs_alloc(sqlite3 *db)
{
    ffs *fs;
    ffs *p;

    fs = fs_new(db);
    alloc_ready(fs, &p);
    return p;
}

static sqlite3 *db_close(sqlite3 *db) /* LCOV_EXCL_LINE */
{
    if (db != NULL)        /* LCOV_EXCL_LINE */
    {                      /* LCOV_EXCL_LINE */
        sqlite3_close(db); /* LCOV_EXCL_LINE */
    }
    return NULL; /* LCOV_EXCL_LINE */
}

static void db_if_ok(int rc, sqlite3 *db, sqlite3 **out)
{
    if (rc == SQLITE_OK)
    {
        *out = db;
        return;
    }
    *out = db_close(db); /* LCOV_EXCL_LINE */
}

static sqlite3 *db_open(const char *path)
{
    sqlite3 *db;
    int rc;

    db = NULL;
    rc = sqlite3_open(path, &db);
    db_if_ok(rc, db, &db);
    return db;
}

static void from_close_if(ffs *fs, sqlite3 *db)
{
    if (fs != NULL)
    {
        return;
    }
    sqlite3_close(db); /* LCOV_EXCL_LINE */
}

static ffs *fs_from(sqlite3 *db)
{
    ffs *fs;

    fs = ffs_alloc(db);
    from_close_if(fs, db);
    return fs;
}

void *choose_ptr(int on, void *on_true, void *on_false)
{
    if (on)
    {
        return on_true;
    }
    return on_false;
}

static ffs *db_fs(sqlite3 *db)
{
    ffs *fs;

    fs = fs_from(db);
    return choose_ptr(db != NULL, fs, NULL);
}

static void create_if(ffs **fs, const char *path)
{
    sqlite3 *db;

    if (path == NULL)
    {
        return;
    }
    db = db_open(path);
    *fs = db_fs(db);
}

ffs *ffs_create(const char *path)
{
    ffs *fs;

    fs = NULL;
    create_if(&fs, path);
    return fs;
}

ffs *ffs_create_memory(void)
{
    sqlite3 *db;
    ffs *fs;

    db = db_open(":memory:");
    fs = db_fs(db);
    return fs;
}

static void destroy_db(ffs *fs)
{
    sqlite3_close(fs->db);
    free(fs);
}

void ffs_destroy(ffs *fs)
{
    if (fs == NULL)
    {
        return;
    }
    destroy_db(fs);
}

static void fs_need(const ffs *fs, int *r)
{
    *r = 0;
    if (fs == NULL)
    {
        *r = util_fail(EINVAL);
    }
}

static void atime_if_ok(ffs *fs, int on, int *r)
{
    if (*r != 0)
    {
        return;
    }
    fs->atime_on = on != 0;
}

int ffs_setatime(ffs *fs, int on)
{
    int r;

    fs_need(fs, &r);
    atime_if_ok(fs, on, &r);
    return r;
}

static void ids_if_ok(ffs *fs, unsigned long uid, unsigned long gid, int *r)
{
    if (*r != 0)
    {
        return;
    }
    fs->uid = (long)uid;
    fs->gid = (long)gid;
}

int ffs_setids(ffs *fs, unsigned long uid, unsigned long gid)
{
    int r;

    fs_need(fs, &r);
    ids_if_ok(fs, uid, gid, &r);
    return r;
}

static void umask_if_ok(ffs *fs, unsigned long mask, int *r)
{
    if (*r != 0)
    {
        return;
    }
    fs->umask = mask & FFS_ALL_PERM;
}

int ffs_setumask(ffs *fs, unsigned long mask)
{
    int r;

    fs_need(fs, &r);
    umask_if_ok(fs, mask, &r);
    return r;
}

long stat_count(ffs *fs, const char *sql)
{
    sqlite3_stmt *s;
    long v;

    s = prepare(fs, sql);
    v = q_i64(s, -EIO);
    return v;
}
