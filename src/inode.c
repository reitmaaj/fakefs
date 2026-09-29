#include "fakefs_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define SQL_INO_MODE "SELECT mode FROM inodes WHERE ino = ?"
#define SQL_INO_PARENT "SELECT parent FROM inodes WHERE ino = ?"
#define SQL_INO_TARGET "SELECT target FROM inodes WHERE ino = ?"
#define SQL_INO_FULL                                                           \
    "SELECT parent,mode,uid,gid,nlink,size,atime,mtime,ctime,target"           \
    " FROM inodes WHERE ino = ?"
#define SQL_INO_INSERT                                                         \
    "INSERT INTO inodes"                                                       \
    " (parent,mode,uid,gid,nlink,size,atime,mtime,ctime,target)"               \
    " VALUES (?,?,?,?,?,?,?,?,?,?)"
#define SQL_INO_UPDATE                                                         \
    "UPDATE inodes SET parent=?,mode=?,uid=?,gid=?,nlink=?,size=?,"            \
    "atime=?,mtime=?,ctime=?,target=? WHERE ino=?"
#define SQL_INO_DELETE "DELETE FROM inodes WHERE ino = ?"
#define SQL_INO_META "SELECT mode,uid,gid FROM inodes WHERE ino = ?"

static sqlite3_stmt *prep_ino(ffs *fs, const char *sql, long ino)
{
    sqlite3_stmt *s;

    s = prepare(fs, sql);
    sql_bind_i64(s, 1, ino);
    return s;
}

static int fin_err(sqlite3_stmt *s, int err)
{
    sql_fin(s);
    return err;
}

int q_code(long v, long err)
{
    if (v == err)
    {
        return (int)v;
    }
    return 0;
}

static int row_i64_out(sqlite3_stmt *s, long *out, long err)
{
    long v;

    v = q_i64(s, err);
    *out = v;
    return q_code(v, err);
}

int kind_is_text(int kind)
{
    if (kind == SQLITE_TEXT)
    {
        return 1;
    }
    return 0;
}

static int row_target_ok(sqlite3_stmt *s)
{
    int isrow;
    int kind;

    isrow = sql_row(s);
    if (isrow == 0)
    {
        return 0;
    }
    kind = sqlite3_column_type(s, 0);
    return kind_is_text(kind);
}

int target_code(const char *p)
{
    if (p != NULL)
    {
        return 0;
    }
    return -ENOMEM;
}

static void row_target(sqlite3_stmt *s, char **target, int *out)
{
    const unsigned char *txt;
    int ok;

    ok = row_target_ok(s);
    if (ok == 0)
    {
        *out = fin_err(s, -ENOENT);
        return;
    }
    txt = sqlite3_column_text(s, 0);
    *target = xstrdup((const char *)txt);
    sql_fin(s);
    *out = target_code(*target);
}

static void fi_zero(struct finode *fi)
{
    memset(fi, 0, sizeof(*fi));
}

static void fi_meta_row(struct finode *fi, sqlite3_stmt *s)
{
    fi->parent = sql_col_i64(s, 0);
    fi->mode = sql_col_i64(s, 1);
    fi->uid = sql_col_i64(s, 2);
    fi->gid = sql_col_i64(s, 3);
    fi->nlink = sql_col_i64(s, 4);
}

static void fi_attrs_row(struct finode *fi, sqlite3_stmt *s)
{
    fi->size = sql_col_i64(s, 5);
    fi->atime = sql_col_i64(s, 6);
    fi->mtime = sql_col_i64(s, 7);
    fi->ctime = sql_col_i64(s, 8);
}

static int fi_target_copy(struct finode *fi, sqlite3_stmt *s)
{
    const unsigned char *txt;

    txt = sqlite3_column_text(s, 9);
    fi->target = xstrdup((const char *)txt);
    return target_code(fi->target);
}

static int fi_target(struct finode *fi, sqlite3_stmt *s)
{
    int kind;
    int r;

    kind = sqlite3_column_type(s, 9);
    if (kind != SQLITE_TEXT)
    {
        return 0;
    }
    r = fi_target_copy(fi, s);
    return r;
}

static int load_extras(struct finode *fi, sqlite3_stmt *s)
{
    int r;

    r = fi_target(fi, s);
    return r;
}

static void fi_row(struct finode *fi, long ino, sqlite3_stmt *s)
{
    fi->ino = ino;
    fi_meta_row(fi, s);
    fi_attrs_row(fi, s);
}

static int load_rows(sqlite3_stmt *s, struct finode *fi, long ino)
{
    int isrow;
    int r;

    isrow = sql_row(s);
    if (isrow == 0)
    {
        return -ENOENT;
    }
    fi_row(fi, ino, s);
    r = load_extras(fi, s);
    return r;
}

static int finish_load(sqlite3_stmt *s, struct finode *fi, int r)
{
    if (r != 0)
    {
        inode_free(fi);
    }
    sql_fin(s);
    return r;
}

static int load_ino(ffs *fs, long ino, struct finode *fi)
{
    sqlite3_stmt *s;
    int r;

    s = prep_ino(fs, SQL_INO_FULL, ino);
    fi_zero(fi);
    r = load_rows(s, fi, ino);
    r = finish_load(s, fi, r);
    return r;
}

void inode_free(struct finode *fi)
{
    free(fi->target);
    fi->target = NULL;
}

static void fi_bind_head(sqlite3_stmt *s, const struct finode *fi)
{
    sql_bind_i64(s, 1, fi->parent);
    sql_bind_i64(s, 2, fi->mode);
    sql_bind_i64(s, 3, fi->uid);
    sql_bind_i64(s, 4, fi->gid);
    sql_bind_i64(s, 5, fi->nlink);
}

static void fi_bind_tail(sqlite3_stmt *s, const struct finode *fi)
{
    sql_bind_i64(s, 6, fi->size);
    sql_bind_i64(s, 7, fi->atime);
    sql_bind_i64(s, 8, fi->mtime);
    sql_bind_i64(s, 9, fi->ctime);
}

static void bind_text_or_null(sqlite3_stmt *s, int i, const char *v)
{
    if (v != NULL)
    {
        sqlite3_bind_text(s, i, v, -1, SQLITE_TRANSIENT);
        return;
    }
    sqlite3_bind_null(s, i);
}

static void fi_bind_target(sqlite3_stmt *s, const struct finode *fi)
{
    bind_text_or_null(s, 10, fi->target);
}

static void fi_bind_new(sqlite3_stmt *s, const struct finode *fi)
{
    fi_bind_head(s, fi);
    fi_bind_tail(s, fi);
    fi_bind_target(s, fi);
}

static long last_id(ffs *fs)
{
    long id;

    id = sqlite3_last_insert_rowid(fs->db);
    return id;
}

static long id_code(ffs *fs, int r)
{
    long id;

    id = -EIO;
    if (r == 0)
    {
        id = last_id(fs);
    }
    return id;
}

static long insert_id(ffs *fs, sqlite3_stmt *s)
{
    int r;
    long id;

    r = sql_done(s);
    id = id_code(fs, r);
    return id;
}

static void fi_bind_store(sqlite3_stmt *s, const struct finode *fi)
{
    fi_bind_new(s, fi);
    sql_bind_i64(s, 11, fi->ino);
}

static void meta_row(sqlite3_stmt *s, long *mode, long *uid, long *gid)
{
    *mode = sql_col_i64(s, 0);
    *uid = sql_col_i64(s, 1);
    *gid = sql_col_i64(s, 2);
}

static int row_meta(sqlite3_stmt *s, long *mode, long *uid, long *gid)
{
    int isrow;
    int r;

    isrow = sql_row(s);
    if (isrow == 0)
    {
        r = fin_err(s, -ENOENT); /* LCOV_EXCL_LINE */
        return r;
    }
    meta_row(s, mode, uid, gid);
    sql_fin(s);
    return 0;
}

int mode_is_dir(long mode)
{
    unsigned long bits;

    bits = (unsigned long)mode;
    return (bits & FFS_S_IFMT) == FFS_S_IFDIR;
}

struct trunc_ctx
{
    long ino;
    ffs_off len;
};

static int trunc_ino(ffs *fs, long ino, ffs_off len);

static int trunc_ino_work(ffs *fs, void *v)
{
    struct trunc_ctx *c;
    int r;

    c = (struct trunc_ctx *)v;
    r = trunc_ino(fs, c->ino, c->len);
    return r;
}

int trunc_dir_ok(const struct finode *fi)
{
    if (mode_is_dir(fi->mode))
    {
        return -EISDIR;
    }
    return 0;
}

static void trunc_meta(struct finode *fi, ffs_off len, long t)
{
    fi->size = len;
    fi->mtime = t;
    fi->ctime = t;
}

static int trunc_store(ffs *fs, struct finode *fi, ffs_off len)
{
    long t;
    int r;

    t = now();
    trunc_meta(fi, len, t);
    r = inode_store(fs, fi);
    return r;
}

static void trunc_store_if(ffs *fs, struct finode *fi, ffs_off len, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = trunc_store(fs, fi, len);
}

static void trunc_data_if(ffs *fs, struct finode *fi, ffs_off len, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = data_truncate(fs, fi->ino, len);
    trunc_store_if(fs, fi, len, r);
}

static int trunc_apply(ffs *fs, struct finode *fi, ffs_off len)
{
    int r;

    r = trunc_dir_ok(fi);
    trunc_data_if(fs, fi, len, &r);
    return r;
}

static int trunc_free(ffs *fs, struct finode *fi, ffs_off len)
{
    int r;

    r = trunc_apply(fs, fi, len);
    inode_free(fi);
    return r;
}

static void trunc_free_if(ffs *fs, struct finode *fi, ffs_off len, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = trunc_free(fs, fi, len);
}

static int trunc_ino(ffs *fs, long ino, ffs_off len)
{
    struct finode fi;
    int r;

    r = inode_load(fs, ino, &fi);
    trunc_free_if(fs, &fi, len, &r);
    return r;
}

int truncate_ino(ffs *fs, long ino, ffs_off len)
{
    struct trunc_ctx c;
    int r;

    c.ino = ino;
    c.len = len;
    r = txn_run(fs, trunc_ino_work, &c);
    return r;
}

int no_refs(ffs *fs, long ino)
{
    return ino_fd_refs(fs, ino) == 0;
}

static int nlink_done(ffs *fs, long ino, long nlink)
{
    if (nlink > 0)
    {
        return 0;
    }
    return no_refs(fs, ino);
}

static int drop_delete(ffs *fs, struct finode *fi, long ino)
{
    int r;

    inode_free(fi);
    r = inode_delete(fs, ino);
    return r;
}

static int drop_store(ffs *fs, struct finode *fi, long t)
{
    int r;

    fi->ctime = t;
    r = inode_store(fs, fi);
    return r;
}

static int drop_save(ffs *fs, struct finode *fi, long t)
{
    int r;

    r = drop_store(fs, fi, t);
    inode_free(fi);
    return r;
}

static int drop_pick(ffs *fs, struct finode *fi, long ino, long t, int gone)
{
    int r;

    if (gone)
    {
        r = drop_delete(fs, fi, ino);
        return r;
    }
    r = drop_save(fs, fi, t);
    return r;
}

static int drop_after(ffs *fs, struct finode *fi, long ino, long t)
{
    int r;
    int gone;

    --fi->nlink;
    gone = nlink_done(fs, ino, fi->nlink);
    r = drop_pick(fs, fi, ino, t, gone);
    return r;
}

static void drop_after_if(ffs *fs, struct finode *fi, long ino, long t, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = drop_after(fs, fi, ino, t);
}

int drop_link(ffs *fs, long ino, long t)
{
    struct finode fi;
    int r;

    r = inode_load(fs, ino, &fi);
    drop_after_if(fs, &fi, ino, t, &r);
    return r;
}

int inode_mode(ffs *fs, long ino, long *mode)
{
    sqlite3_stmt *s;
    int r;

    s = prep_ino(fs, SQL_INO_MODE, ino);
    r = row_i64_out(s, mode, -ENOENT);
    return r;
}

int inode_parent(ffs *fs, long ino, long *parent)
{
    sqlite3_stmt *s;
    int r;

    s = prep_ino(fs, SQL_INO_PARENT, ino);
    r = row_i64_out(s, parent, -ENOENT);
    return r;
}

int inode_target(ffs *fs, long ino, char **target)
{
    sqlite3_stmt *s;
    int r;

    s = prep_ino(fs, SQL_INO_TARGET, ino);
    *target = NULL;
    row_target(s, target, &r);
    return r;
}

int inode_load(ffs *fs, long ino, struct finode *fi)
{
    int r;

    r = load_ino(fs, ino, fi);
    return r;
}

long inode_new(ffs *fs, const struct finode *fi)
{
    sqlite3_stmt *s;
    long id;

    s = prepare(fs, SQL_INO_INSERT);
    fi_bind_new(s, fi);
    id = insert_id(fs, s);
    return id;
}

int inode_store(ffs *fs, const struct finode *fi)
{
    sqlite3_stmt *s;
    int r;

    s = prepare(fs, SQL_INO_UPDATE);
    fi_bind_store(s, fi);
    r = sql_done(s);
    return r;
}

struct del_ctx
{
    long ino;
};

static int inode_delete_row(ffs *fs, long ino)
{
    sqlite3_stmt *s;
    int r;

    s = prep_ino(fs, SQL_INO_DELETE, ino);
    r = sql_done(s);
    return r;
}

static void del_row_if(ffs *fs, long ino, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = inode_delete_row(fs, ino);
}

static int inode_del_run(ffs *fs, long ino)
{
    int r;

    r = data_delete(fs, ino);
    del_row_if(fs, ino, &r);
    return r;
}

static int inode_del_work(ffs *fs, void *v)
{
    struct del_ctx *c;
    int r;

    c = (struct del_ctx *)v;
    r = inode_del_run(fs, c->ino);
    return r;
}

int inode_delete(ffs *fs, long ino)
{
    struct del_ctx c;
    int r;

    c.ino = ino;
    r = txn_run(fs, inode_del_work, &c);
    return r;
}

int inode_meta(ffs *fs, long ino, long *mode, long *uid, long *gid)
{
    sqlite3_stmt *s;
    int r;

    s = prep_ino(fs, SQL_INO_META, ino);
    r = row_meta(s, mode, uid, gid);
    return r;
}
