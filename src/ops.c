#include "fakefs_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SQL_STATFS_FILES "SELECT COUNT(*) FROM inodes"
#define SQL_STATFS_BLOCKS "SELECT COALESCE(SUM(size),0) FROM inodes"

struct path_res
{
    long parent;
    long ino;
    char name[FFS_NAME_MAX + 1];
};

struct mkdir_ctx
{
    long parent;
    unsigned long mode;
    long t;
    char name[FFS_NAME_MAX + 1];
};

struct unlink_ctx
{
    long parent;
    long ino;
    int rmdir;
    long t;
    char name[FFS_NAME_MAX + 1];
};

struct link_ctx
{
    long np;
    long oino;
    long t;
    char nname[FFS_NAME_MAX + 1];
};

struct symlink_ctx
{
    long parent;
    long ino;
    const char *target;
    long t;
    char name[FFS_NAME_MAX + 1];
};

struct rename_ctx
{
    long op;
    long np;
    long oino;
    long nino;
    int odir;
    int texists;
    int same;
    long t;
    char oname[FFS_NAME_MAX + 1];
    char nname[FFS_NAME_MAX + 1];
};

struct open_ctx
{
    const char *path;
    long parent;
    long ino;
    unsigned long mode;
    long t;
    int flags;
    int create;
    int made;
    int dirflag;
    int isdir;
    int follow;
    int of;
    int fd;
    char name[FFS_NAME_MAX + 1];
};

static void of_release(ffs *fs, int of);

static int txn_done(ffs *fs, int (*op)(ffs *, void *), void *ctx)
{
    int r;
    int out;

    r = txn_run(fs, op, ctx);
    if (r < 0)
    {
        out = util_fail_r(r);
        return out;
    }
    return 0;
}

static void fi_reset(struct finode *fi)
{
    memset(fi, 0, sizeof(*fi));
}

static void fi_ident(struct finode *fi, long parent, ffs *fs)
{
    fi->parent = parent;
    fi->uid = fs->uid;
    fi->gid = fs->gid;
}

static void fi_times(struct finode *fi, long t)
{
    fi->atime = t;
    fi->mtime = t;
    fi->ctime = t;
}

static long masked_mode(ffs *fs, long type, unsigned long mode)
{
    unsigned long m;
    unsigned long u;

    m = mode & FFS_ALL_PERM;
    u = ~fs->umask;
    m = m & u;
    return (long)((unsigned long)type | m);
}

static struct finode finode_new(ffs *fs, long parent, long mode)
{
    struct finode fi;

    fi_reset(&fi);
    fi_ident(&fi, parent, fs);
    fi.mode = mode;
    fi.nlink = 1;
    return fi;
}

static struct finode symlink_finode(ffs *fs, long parent, const char *target)
{
    struct finode fi;

    fi = finode_new(fs, parent, FFS_S_IFLNK | FFS_ALL_PERM);
    fi.size = (long)strlen(target);
    fi.target = xstrdup(target);
    return fi;
}

static int nofollow(int flags)
{
    if (flags & AT_SYMLINK_NOFOLLOW)
    {
        return 0;
    }
    return 1;
}

static int follow_flag(int flags)
{
    if (flags & AT_SYMLINK_FOLLOW)
    {
        return 1;
    }
    return 0;
}

static int resolve_exist(ffs *fs, int dirfd, const char *path, long *parent,
                         char *name)
{
    long ino;
    int r;

    name[0] = '\0';
    r = resolve_at(fs, dirfd, path, 0, 0, parent, name, &ino);
    return r;
}

int find_code(int r)
{
    if (r == 0)
    {
        return -EEXIST;
    }
    return choose(r == -ENOENT, 0, r);
}

static int resolve_find(ffs *fs, int dirfd, const char *path, long *parent,
                        char *name)
{
    int r;

    r = resolve_exist(fs, dirfd, path, parent, name);
    return find_code(r);
}

int new_code(int r, const char *name)
{
    if (r != 0)
    {
        return r;
    }
    return choose(name[0] == '\0', -ENOENT, 0);
}

static int resolve_new(ffs *fs, int dirfd, const char *path, long *parent,
                       char *name)
{
    int r;

    r = resolve_find(fs, dirfd, path, parent, name);
    return new_code(r, name);
}

static int mkdir_check(ffs *fs, const char *path)
{
    if (either_null(fs, path))
    {
        return -EINVAL;
    }
    return 0;
}

static void mkdir_locate_if(ffs *fs, int dirfd, const char *path,
                            struct mkdir_ctx *ctx, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = resolve_new(fs, dirfd, path, &ctx->parent, ctx->name);
}

static int mkdirat_resolve(ffs *fs, int dirfd, const char *path,
                           struct mkdir_ctx *ctx)
{
    int r;

    r = mkdir_check(fs, path);
    mkdir_locate_if(fs, dirfd, path, ctx, &r);
    return r;
}

static int mkdir_perm(ffs *fs, struct mkdir_ctx *ctx, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = check_create_dir(fs, ctx->parent);
    return r;
}

static int mkdirat_prep(ffs *fs, int dirfd, const char *path,
                        struct mkdir_ctx *ctx)
{
    int r;

    r = mkdirat_resolve(fs, dirfd, path, ctx);
    r = mkdir_perm(fs, ctx, r);
    return r;
}

static struct finode mkdir_finode(ffs *fs, struct mkdir_ctx *ctx)
{
    struct finode fi;
    long mode;

    mode = masked_mode(fs, FFS_S_IFDIR, ctx->mode);
    fi = finode_new(fs, ctx->parent, mode);
    fi_times(&fi, ctx->t);
    return fi;
}

static long mkdir_inode(ffs *fs, struct mkdir_ctx *ctx)
{
    struct finode fi;
    long ino;

    fi = mkdir_finode(fs, ctx);
    ino = inode_new(fs, &fi);
    return ino;
}

static int parent_nlink(ffs *fs, long parent, long delta)
{
    int r;

    r = dir_nlink(fs, parent, delta);
    return choose(r < 0, r, 0);
}

static int touch_if(ffs *fs, long ino, long t, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = dir_touch(fs, ino, t);
    return r;
}

static void mkdir_touch_if(ffs *fs, struct mkdir_ctx *ctx, int *r)
{
    if (*r < 0)
    {
        return; /* LCOV_EXCL_LINE */
    }
    *r = parent_nlink(fs, ctx->parent, 1);
    *r = touch_if(fs, ctx->parent, ctx->t, *r);
}

static void mkdir_dirents_if(ffs *fs, struct mkdir_ctx *ctx, long ino, int *r)
{
    if (*r < 0)
    {
        return; /* LCOV_EXCL_LINE */
    }
    *r = dirent_insert(fs, ctx->parent, ctx->name, ino);
    mkdir_touch_if(fs, ctx, r);
}

static int mkdir_add(ffs *fs, struct mkdir_ctx *ctx)
{
    long ino;
    int r;

    ino = mkdir_inode(fs, ctx);
    r = neg_code(ino);
    mkdir_dirents_if(fs, ctx, ino, &r);
    return r;
}

static int mkdir_work(ffs *fs, void *v)
{
    struct mkdir_ctx *ctx;
    int r;

    ctx = (struct mkdir_ctx *)v;
    ctx->t = now();
    r = mkdir_add(fs, ctx);
    return r;
}

static int mkdir_txn(ffs *fs, struct mkdir_ctx *ctx, int r)
{
    if (r != 0)
    {
        return r;
    }
    r = txn_done(fs, mkdir_work, ctx);
    return r;
}

static int mkdir_run(ffs *fs, int dirfd, const char *path,
                     struct mkdir_ctx *ctx)
{
    int r;

    r = mkdirat_prep(fs, dirfd, path, ctx);
    r = mkdir_txn(fs, ctx, r);
    return r;
}

int ffs_mkdirat(ffs *fs, int dirfd, const char *path, unsigned long mode)
{
    struct mkdir_ctx ctx;
    int r;

    ctx.mode = mode;
    r = mkdir_run(fs, dirfd, path, &ctx);
    r = fail_code(r);
    return r;
}

int ffs_mkdir(ffs *fs, const char *path, unsigned long mode)
{
    int r;

    r = ffs_mkdirat(fs, AT_FDCWD, path, mode);
    return r;
}

static int rmdir_code(ffs *fs, struct unlink_ctx *ctx)
{
    int d;
    int r;

    d = is_dir(fs, ctx->ino);
    r = choose(d == 0, -ENOTDIR, 0);
    return choose(ctx->ino == fs->root, -EBUSY, r);
}

static int rmdir_count_ok(ffs *fs, long ino)
{
    int n;

    n = dir_count(fs, ino);
    return choose(n > 0, -ENOTEMPTY, 0);
}

static int count_if(ffs *fs, struct unlink_ctx *ctx, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = rmdir_count_ok(fs, ctx->ino);
    return r;
}

static int unlink_dir_code(ffs *fs, struct unlink_ctx *ctx)
{
    int r;

    r = rmdir_code(fs, ctx);
    r = count_if(fs, ctx, r);
    return r;
}

static int unlink_file_code(ffs *fs, struct unlink_ctx *ctx)
{
    int r;

    r = is_dir(fs, ctx->ino);
    return choose(r, -EISDIR, 0);
}

int name_empty(const char *name)
{
    return name[0] == '\0';
}

static int name_code(struct unlink_ctx *ctx)
{
    if (name_empty(ctx->name))
    {
        return -EINVAL;
    }
    return 0;
}

static int unlink_dispatch(ffs *fs, struct unlink_ctx *ctx)
{
    int r;
    int d;

    r = unlink_dir_code(fs, ctx);
    d = unlink_file_code(fs, ctx);
    return choose(ctx->rmdir, r, d);
}

static int unlink_named(ffs *fs, struct unlink_ctx *ctx, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = unlink_dispatch(fs, ctx);
    return r;
}

static int name_code_if(struct unlink_ctx *ctx, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = name_code(ctx);
    return r;
}

static int unlink_resolved(ffs *fs, struct unlink_ctx *ctx, int r)
{
    r = name_code_if(ctx, r);
    r = unlink_named(fs, ctx, r);
    return r;
}

static int unlink_resolve(ffs *fs, int dirfd, const char *path,
                          struct unlink_ctx *ctx)
{
    int r;

    r = resolve_at(fs, dirfd, path, 0, 0, &ctx->parent, ctx->name, &ctx->ino);
    r = unlink_resolved(fs, ctx, r);
    return r;
}

static void unlink_guard_if(ffs *fs, int dirfd, const char *path,
                            struct unlink_ctx *ctx, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = unlink_resolve(fs, dirfd, path, ctx);
}

static int unlink_validate(ffs *fs, int dirfd, const char *path, int flags,
                           struct unlink_ctx *ctx)
{
    int r;

    r = choose(either_null(fs, path), -EINVAL, 0);
    ctx->rmdir = (flags & AT_REMOVEDIR) != 0;
    unlink_guard_if(fs, dirfd, path, ctx, &r);
    return r;
}

static void unlink_nlink_if(ffs *fs, struct unlink_ctx *ctx, int *r)
{
    if (ctx->rmdir == 0)
    {
        return;
    }
    *r = parent_nlink(fs, ctx->parent, -1);
}

static int unlink_after(ffs *fs, struct unlink_ctx *ctx)
{
    int r;

    r = 0;
    unlink_nlink_if(fs, ctx, &r);
    r = touch_if(fs, ctx->parent, ctx->t, r);
    return r;
}

static void drop_dir_if(ffs *fs, struct unlink_ctx *ctx, int *r)
{
    if (ctx->rmdir == 0)
    {
        return;
    }
    *r = inode_delete(fs, ctx->ino);
}

static void drop_file_if(ffs *fs, struct unlink_ctx *ctx, int *r)
{
    if (ctx->rmdir)
    {
        return;
    }
    *r = drop_link(fs, ctx->ino, ctx->t);
}

static int unlink_drop(ffs *fs, struct unlink_ctx *ctx)
{
    int r;

    r = 0;
    drop_dir_if(fs, ctx, &r);
    drop_file_if(fs, ctx, &r);
    return r;
}

static void unlink_drop_if(ffs *fs, struct unlink_ctx *ctx, int *r)
{
    if (*r < 0)
    {
        return; /* LCOV_EXCL_LINE */
    }
    *r = unlink_drop(fs, ctx);
}

static void unlink_after_if(ffs *fs, struct unlink_ctx *ctx, int *r)
{
    if (*r < 0)
    {
        return; /* LCOV_EXCL_LINE */
    }
    *r = unlink_after(fs, ctx);
}

static int unlink_remove(ffs *fs, struct unlink_ctx *ctx)
{
    int r;

    r = dirent_delete(fs, ctx->parent, ctx->name);
    unlink_drop_if(fs, ctx, &r);
    unlink_after_if(fs, ctx, &r);
    return r;
}

static int unlink_work(ffs *fs, void *v)
{
    struct unlink_ctx *ctx;
    int r;

    ctx = (struct unlink_ctx *)v;
    ctx->t = now();
    r = unlink_remove(fs, ctx);
    return r;
}

static int unlink_txn(ffs *fs, struct unlink_ctx *ctx, int r)
{
    if (r != 0)
    {
        return r;
    }
    r = txn_done(fs, unlink_work, ctx);
    return r;
}

static int unlink_run(ffs *fs, int dirfd, const char *path, int flags,
                      struct unlink_ctx *ctx)
{
    int r;

    r = unlink_validate(fs, dirfd, path, flags, ctx);
    r = unlink_txn(fs, ctx, r);
    return r;
}

int ffs_unlinkat(ffs *fs, int dirfd, const char *path, int flags)
{
    struct unlink_ctx ctx;
    int r;

    r = unlink_run(fs, dirfd, path, flags, &ctx);
    r = fail_code(r);
    return r;
}

int ffs_unlink(ffs *fs, const char *path)
{
    int r;

    r = ffs_unlinkat(fs, AT_FDCWD, path, 0);
    return r;
}

int ffs_rmdir(ffs *fs, const char *path)
{
    int r;

    r = ffs_unlinkat(fs, AT_FDCWD, path, AT_REMOVEDIR);
    return r;
}

static int link_dir_ok(ffs *fs, long ino)
{
    int r;

    r = is_dir(fs, ino);
    return choose(r, -EPERM, 0);
}

static int link_dir_if(ffs *fs, struct link_ctx *ctx, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = link_dir_ok(fs, ctx->oino);
    return r;
}

static int link_old(ffs *fs, int olddirfd, const char *oldpath, int flags,
                    struct link_ctx *ctx)
{
    int r;

    r = resolve_at(fs, olddirfd, oldpath, follow_flag(flags), 0, &ctx->np,
                   ctx->nname, &ctx->oino);
    r = link_dir_if(fs, ctx, r);
    return r;
}

static int check_create_if(ffs *fs, long ino, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = check_create_dir(fs, ino);
    return r;
}

static int link_target(ffs *fs, int newdirfd, const char *newpath,
                       struct link_ctx *ctx)
{
    int r;

    r = resolve_new(fs, newdirfd, newpath, &ctx->np, ctx->nname);
    r = check_create_if(fs, ctx->np, r);
    return r;
}

static void link_new_if(ffs *fs, int newdirfd, const char *newpath,
                        struct link_ctx *ctx, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = link_target(fs, newdirfd, newpath, ctx);
}

static void link_old_if(ffs *fs, int olddirfd, const char *oldpath, int flags,
                        struct link_ctx *ctx, int *r)
{
    if (*r < 0)
    {
        return; /* LCOV_EXCL_LINE */
    }
    *r = link_old(fs, olddirfd, oldpath, flags, ctx);
}

static int linkat_prep(ffs *fs, int olddirfd, const char *oldpath, int newdirfd,
                       const char *newpath, int flags, struct link_ctx *ctx)
{
    int r;

    r = choose(any_null3(fs, oldpath, newpath), -EINVAL, 0);
    link_old_if(fs, olddirfd, oldpath, flags, ctx, &r);
    link_new_if(fs, newdirfd, newpath, ctx, &r);
    return r;
}

static int bump_store(ffs *fs, struct finode *fi, long t)
{
    int r;

    ++fi->nlink;
    fi->ctime = t;
    r = inode_store(fs, fi);
    return r;
}

static int bump_free(ffs *fs, struct finode *fi, long t)
{
    int r;

    r = bump_store(fs, fi, t);
    inode_free(fi);
    return r;
}

static void bump_loaded(ffs *fs, struct finode *fi, long t, int *r)
{
    if (*r < 0)
    {
        return; /* LCOV_EXCL_LINE */
    }
    *r = bump_free(fs, fi, t);
}

static int link_bump(ffs *fs, long ino, long t)
{
    struct finode fi;
    int r;

    r = inode_load(fs, ino, &fi);
    bump_loaded(fs, &fi, t, &r);
    return r;
}

static int link_touch(ffs *fs, struct link_ctx *ctx)
{
    int r;

    r = link_bump(fs, ctx->oino, ctx->t);
    r = touch_if(fs, ctx->np, ctx->t, r);
    return r;
}

static int link_touch_if(ffs *fs, struct link_ctx *ctx, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = link_touch(fs, ctx);
    return r;
}

static int link_insert(ffs *fs, struct link_ctx *ctx)
{
    int r;

    r = dirent_insert(fs, ctx->np, ctx->nname, ctx->oino);
    r = link_touch_if(fs, ctx, r);
    return r;
}

static int link_work(ffs *fs, void *v)
{
    struct link_ctx *ctx;
    int r;

    ctx = (struct link_ctx *)v;
    ctx->t = now();
    r = link_insert(fs, ctx);
    return r;
}

static int link_txn(ffs *fs, struct link_ctx *ctx, int r)
{
    if (r != 0)
    {
        return r;
    }
    r = txn_done(fs, link_work, ctx);
    return r;
}

static int link_run(ffs *fs, int olddirfd, const char *oldpath, int newdirfd,
                    const char *newpath, int flags, struct link_ctx *ctx)
{
    int r;

    r = linkat_prep(fs, olddirfd, oldpath, newdirfd, newpath, flags, ctx);
    r = link_txn(fs, ctx, r);
    return r;
}

int ffs_linkat(ffs *fs, int olddirfd, const char *oldpath, int newdirfd,
               const char *newpath, int flags)
{
    struct link_ctx ctx;
    int r;

    r = link_run(fs, olddirfd, oldpath, newdirfd, newpath, flags, &ctx);
    r = fail_code(r);
    return r;
}

int ffs_link(ffs *fs, const char *oldpath, const char *newpath)
{
    int r;

    r = ffs_linkat(fs, AT_FDCWD, oldpath, AT_FDCWD, newpath, 0);
    return r;
}

static int symlink_new(ffs *fs, int newdirfd, const char *linkpath,
                       struct symlink_ctx *ctx)
{
    int r;

    r = resolve_new(fs, newdirfd, linkpath, &ctx->parent, ctx->name);
    r = check_create_if(fs, ctx->parent, r);
    return r;
}

static void symlink_new_if(ffs *fs, int newdirfd, const char *linkpath,
                           struct symlink_ctx *ctx, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = symlink_new(fs, newdirfd, linkpath, ctx);
}

static int symlinkat_prep(ffs *fs, const char *target, int newdirfd,
                          const char *linkpath, struct symlink_ctx *ctx)
{
    int r;

    r = choose(any_null3(fs, target, linkpath), -EINVAL, 0);
    ctx->target = target;
    symlink_new_if(fs, newdirfd, linkpath, ctx, &r);
    return r;
}

static struct finode symlink_fi(ffs *fs, struct symlink_ctx *ctx)
{
    struct finode fi;

    fi = symlink_finode(fs, ctx->parent, ctx->target);
    fi_times(&fi, ctx->t);
    return fi;
}

static long symlink_inode(ffs *fs, struct symlink_ctx *ctx)
{
    struct finode fi;
    long ino;

    fi = symlink_fi(fs, ctx);
    ino = inode_new(fs, &fi);
    inode_free(&fi);
    return ino;
}

static void symlink_dirent_if(ffs *fs, struct symlink_ctx *ctx, int *r)
{
    if (*r < 0)
    {
        return; /* LCOV_EXCL_LINE */
    }
    *r = dirent_insert(fs, ctx->parent, ctx->name, ctx->ino);
    *r = touch_if(fs, ctx->parent, ctx->t, *r);
}

static int symlink_add(ffs *fs, struct symlink_ctx *ctx)
{
    int r;

    ctx->ino = symlink_inode(fs, ctx);
    r = neg_code(ctx->ino);
    symlink_dirent_if(fs, ctx, &r);
    return r;
}

static int symlink_work(ffs *fs, void *v)
{
    struct symlink_ctx *ctx;
    int r;

    ctx = (struct symlink_ctx *)v;
    ctx->t = now();
    r = symlink_add(fs, ctx);
    return r;
}

static int symlink_txn(ffs *fs, struct symlink_ctx *ctx, int r)
{
    if (r != 0)
    {
        return r;
    }
    r = txn_done(fs, symlink_work, ctx);
    return r;
}

static int symlink_run(ffs *fs, const char *target, int newdirfd,
                       const char *linkpath, struct symlink_ctx *ctx)
{
    int r;

    r = symlinkat_prep(fs, target, newdirfd, linkpath, ctx);
    r = symlink_txn(fs, ctx, r);
    return r;
}

int ffs_symlinkat(ffs *fs, const char *target, int newdirfd,
                  const char *linkpath)
{
    struct symlink_ctx ctx;
    int r;

    r = symlink_run(fs, target, newdirfd, linkpath, &ctx);
    r = fail_code(r);
    return r;
}

int ffs_symlink(ffs *fs, const char *target, const char *linkpath)
{
    int r;

    r = ffs_symlinkat(fs, target, AT_FDCWD, linkpath);
    return r;
}

struct readlink_ctx
{
    long parent;
    long ino;
    struct finode fi;
    char name[FFS_NAME_MAX + 1];
};

static void readlink_resolve_if(ffs *fs, int dirfd, const char *path,
                                struct readlink_ctx *ctx, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = resolve_at(fs, dirfd, path, 0, 0, &ctx->parent, ctx->name, &ctx->ino);
}

static int readlink_prep(ffs *fs, int dirfd, const char *path, char *buf,
                         struct readlink_ctx *ctx)
{
    int r;

    r = choose(any_null3(fs, path, buf), -EINVAL, 0);
    readlink_resolve_if(fs, dirfd, path, ctx, &r);
    return r;
}

static int readlink_bad(long mode)
{
    return ((unsigned long)mode & FFS_S_IFMT) != FFS_S_IFLNK;
}

static int readlink_bad_if(struct finode *fi, int r)
{
    if (r < 0)
    {
        return 0; /* LCOV_EXCL_LINE */
    }
    r = readlink_bad(fi->mode);
    return r;
}

static void readlink_free_if(struct finode *fi, int bad)
{
    if (bad)
    {
        inode_free(fi);
    }
}

static int readlink_get(ffs *fs, long ino, struct finode *fi)
{
    int bad;
    int r;

    r = inode_load(fs, ino, fi);
    bad = readlink_bad_if(fi, r);
    readlink_free_if(fi, bad);
    return choose(bad, -EINVAL, r);
}

static void readlink_get_if(ffs *fs, struct readlink_ctx *ctx, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = readlink_get(fs, ctx->ino, &ctx->fi);
}

static int readlink_copy(char *buf, struct finode *fi, size_t bufsz)
{
    size_t n;

    n = min_size((size_t)fi->size, bufsz);
    memcpy(buf, fi->target, n);
    inode_free(fi);
    return (int)n;
}

static int readlink_copy_if(int r, char *buf, struct finode *fi, size_t bufsz)
{
    if (r < 0)
    {
        return r;
    }
    r = readlink_copy(buf, fi, bufsz);
    return r;
}

static ssize_t readlink_result(int r)
{
    if (r < 0)
    {
        r = fail_code(r);
        return (ssize_t)r;
    }
    return (ssize_t)r;
}

static int readlink_done(ffs *fs, int dirfd, const char *path, char *buf,
                         size_t bufsz, struct readlink_ctx *ctx)
{
    int r;

    r = readlink_prep(fs, dirfd, path, buf, ctx);
    readlink_get_if(fs, ctx, &r);
    r = readlink_copy_if(r, buf, &ctx->fi, bufsz);
    return r;
}

static ssize_t readlink_finish(ffs *fs, int dirfd, const char *path, char *buf,
                               size_t bufsz, struct readlink_ctx *ctx)
{
    int r;
    ssize_t out;

    r = readlink_done(fs, dirfd, path, buf, bufsz, ctx);
    out = readlink_result(r);
    return out;
}

ssize_t ffs_readlinkat(ffs *fs, int dirfd, const char *path, char *buf,
                       size_t bufsz)
{
    struct readlink_ctx ctx;
    ssize_t out;

    out = readlink_finish(fs, dirfd, path, buf, bufsz, &ctx);
    return out;
}

ssize_t ffs_readlink(ffs *fs, const char *path, char *buf, size_t bufsz)
{
    ssize_t out;

    out = ffs_readlinkat(fs, AT_FDCWD, path, buf, bufsz);
    return out;
}

int type_code(int odir, int ndir)
{
    if (odir)
    {
        return choose(ndir == 0, -ENOTDIR, 0);
    }
    return choose(ndir, -EISDIR, 0);
}

int guard_skip(int r, int flag)
{
    if (r < 0)
    {
        return 1;
    }
    return flag != 0;
}

static int old_name_code(struct rename_ctx *ctx)
{
    if (name_empty(ctx->oname))
    {
        return -EINVAL;
    }
    return 0;
}

static void old_root_if(ffs *fs, struct rename_ctx *ctx, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = choose(ctx->oino == fs->root, -EBUSY, 0);
}

static int old_ok(ffs *fs, struct rename_ctx *ctx)
{
    int r;

    r = old_name_code(ctx);
    old_root_if(fs, ctx, &r);
    ctx->odir = is_dir(fs, ctx->oino);
    return r;
}

static int old_ok_if(ffs *fs, struct rename_ctx *ctx, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = old_ok(fs, ctx);
    return r;
}

static int rename_old(ffs *fs, int olddirfd, const char *oldpath,
                      struct rename_ctx *ctx)
{
    int r;

    r = resolve_at(fs, olddirfd, oldpath, 0, 0, &ctx->op, ctx->oname,
                   &ctx->oino);
    r = old_ok_if(fs, ctx, r);
    return r;
}

static int there_ancestor(ffs *fs, struct rename_ctx *ctx, int r)
{
    if (guard_skip(r, ctx->odir == 0))
    {
        return r;
    }
    r = is_ancestor(fs, ctx->oino, ctx->np);
    return choose(r, -EINVAL, 0);
}

static int there_count(ffs *fs, struct rename_ctx *ctx, int r)
{
    if (r != 0)
    {
        return r;
    }
    r = dir_count(fs, ctx->nino);
    return choose(r > 0, -ENOTEMPTY, 0);
}

static int there_dir(ffs *fs, struct rename_ctx *ctx, int r)
{
    r = there_ancestor(fs, ctx, r);
    r = there_count(fs, ctx, r);
    return r;
}

static int there_type(ffs *fs, struct rename_ctx *ctx)
{
    int d;
    int r;

    d = is_dir(fs, ctx->nino);
    r = type_code(ctx->odir, d);
    r = there_dir(fs, ctx, r);
    return r;
}

static int there_type_if(ffs *fs, struct rename_ctx *ctx, int r)
{
    if (guard_skip(r, ctx->same))
    {
        return r;
    }
    r = there_type(fs, ctx);
    return r;
}

static int there_name_code(struct rename_ctx *ctx)
{
    if (name_empty(ctx->nname))
    {
        return -EINVAL; /* LCOV_EXCL_LINE */
    }
    return 0;
}

static void there_same_if(struct rename_ctx *ctx, int *r)
{
    if (*r != 0)
    {
        return;
    }
    ctx->same = ctx->nino == ctx->oino;
}

static int rename_there(ffs *fs, struct rename_ctx *ctx)
{
    int r;

    ctx->texists = 1;
    r = there_name_code(ctx);
    there_same_if(ctx, &r);
    r = there_type_if(fs, ctx, r);
    return r;
}

static int missing_name_code(struct rename_ctx *ctx)
{
    if (name_empty(ctx->nname))
    {
        return -ENOENT;
    }
    return 0;
}

static int missing_dir_if(ffs *fs, struct rename_ctx *ctx, int r)
{
    if (r != 0)
    {
        return r;
    }
    r = there_ancestor(fs, ctx, 0);
    return r;
}

static int rename_missing(ffs *fs, struct rename_ctx *ctx)
{
    int r;

    ctx->texists = 0;
    r = missing_name_code(ctx);
    r = missing_dir_if(fs, ctx, r);
    return r;
}

static int rename_exist(ffs *fs, int newdirfd, const char *newpath,
                        struct rename_ctx *ctx)
{
    int r;

    r = resolve_at(fs, newdirfd, newpath, 0, 0, &ctx->np, ctx->nname,
                   &ctx->nino);
    return r;
}

static int target_exist_if(ffs *fs, struct rename_ctx *ctx, int r)
{
    if (r != 0)
    {
        return r;
    }
    r = rename_there(fs, ctx);
    return r;
}

static int target_missing_if(ffs *fs, struct rename_ctx *ctx, int r)
{
    if (r != -ENOENT)
    {
        return r;
    }
    r = rename_missing(fs, ctx);
    return r;
}

static int rename_target(ffs *fs, int newdirfd, const char *newpath,
                         struct rename_ctx *ctx)
{
    int r;

    r = rename_exist(fs, newdirfd, newpath, ctx);
    r = target_exist_if(fs, ctx, r);
    r = target_missing_if(fs, ctx, r);
    return r;
}

static int rename_check(ffs *fs, struct rename_ctx *ctx)
{
    int r;

    r = check_create_dir(fs, ctx->op);
    r = check_create_if(fs, ctx->np, r);
    return r;
}

static int rename_check_if(ffs *fs, struct rename_ctx *ctx, int r)
{
    if (guard_skip(r, ctx->same))
    {
        return r;
    }
    r = rename_check(fs, ctx);
    return r;
}

static int rename_new(ffs *fs, int newdirfd, const char *newpath,
                      struct rename_ctx *ctx)
{
    int r;

    r = rename_target(fs, newdirfd, newpath, ctx);
    r = rename_check_if(fs, ctx, r);
    return r;
}

static void rename_old_if(ffs *fs, int olddirfd, const char *oldpath,
                          struct rename_ctx *ctx, int *r)
{
    if (*r != 0)
    {
        return;
    }
    *r = rename_old(fs, olddirfd, oldpath, ctx);
}

static void rename_new_if(ffs *fs, int newdirfd, const char *newpath,
                          struct rename_ctx *ctx, int *r)
{
    if (*r != 0)
    {
        return;
    }
    *r = rename_new(fs, newdirfd, newpath, ctx);
}

static int rename_prep(ffs *fs, int olddirfd, const char *oldpath, int newdirfd,
                       const char *newpath, struct rename_ctx *ctx)
{
    int r;

    r = choose(any_null3(fs, oldpath, newpath), -EINVAL, 0);
    rename_old_if(fs, olddirfd, oldpath, ctx, &r);
    rename_new_if(fs, newdirfd, newpath, ctx, &r);
    return r;
}

static void remove_dir_if(ffs *fs, struct rename_ctx *ctx, int *r)
{
    if (guard_skip(*r, ctx->odir == 0))
    {
        return;
    }
    *r = inode_delete(fs, ctx->nino);
}

static void remove_file_if(ffs *fs, struct rename_ctx *ctx, int *r)
{
    if (guard_skip(*r, ctx->odir != 0))
    {
        return;
    }
    *r = drop_link(fs, ctx->nino, ctx->t);
}

static int rename_remove_new(ffs *fs, struct rename_ctx *ctx)
{
    int r;

    r = dirent_delete(fs, ctx->np, ctx->nname);
    remove_dir_if(fs, ctx, &r);
    remove_file_if(fs, ctx, &r);
    return r;
}

static int rename_save(ffs *fs, struct finode *fi, struct rename_ctx *ctx)
{
    int r;

    fi->parent = ctx->np;
    fi->ctime = ctx->t;
    r = inode_store(fs, fi);
    return r;
}

static int move_free(ffs *fs, struct finode *fi, struct rename_ctx *ctx)
{
    int r;

    r = rename_save(fs, fi, ctx);
    inode_free(fi);
    return r;
}

static void move_loaded(ffs *fs, struct finode *fi, struct rename_ctx *ctx,
                        int *r)
{
    if (*r < 0)
    {
        return; /* LCOV_EXCL_LINE */
    }
    *r = move_free(fs, fi, ctx);
}

static int rename_move(ffs *fs, struct rename_ctx *ctx)
{
    struct finode fi;
    int r;

    r = inode_load(fs, ctx->oino, &fi);
    move_loaded(fs, &fi, ctx, &r);
    return r;
}

static int move_if(ffs *fs, struct rename_ctx *ctx, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = rename_move(fs, ctx);
    return r;
}

static int rename_insert(ffs *fs, struct rename_ctx *ctx)
{
    int r;

    r = dirent_insert(fs, ctx->np, ctx->nname, ctx->oino);
    r = move_if(fs, ctx, r);
    return r;
}

static void nlink_drop_if(ffs *fs, struct rename_ctx *ctx, int *r)
{
    if (ctx->texists == 0)
    {
        *r = 0;
        return;
    }
    *r = parent_nlink(fs, ctx->np, -1);
}

static int nlink_add_if(ffs *fs, struct rename_ctx *ctx, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = parent_nlink(fs, ctx->np, 1);
    return r;
}

static int nlink_new(ffs *fs, struct rename_ctx *ctx)
{
    int r;

    nlink_drop_if(fs, ctx, &r);
    r = nlink_add_if(fs, ctx, r);
    return r;
}

static int nlink_if(ffs *fs, struct rename_ctx *ctx, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = nlink_new(fs, ctx);
    return r;
}

static int rename_nlinks(ffs *fs, struct rename_ctx *ctx)
{
    int r;

    r = parent_nlink(fs, ctx->op, -1);
    r = nlink_if(fs, ctx, r);
    return r;
}

static int touch_parent_if(ffs *fs, struct rename_ctx *ctx, int r)
{
    if (guard_skip(r, ctx->np == ctx->op))
    {
        return r;
    }
    r = dir_touch(fs, ctx->np, ctx->t);
    return r;
}

static int rename_touch(ffs *fs, struct rename_ctx *ctx)
{
    int r;

    r = dir_touch(fs, ctx->op, ctx->t);
    r = touch_parent_if(fs, ctx, r);
    return r;
}

static void rename_nlinks_if(ffs *fs, struct rename_ctx *ctx, int *r)
{
    if (ctx->odir == 0)
    {
        *r = 0;
        return;
    }
    *r = rename_nlinks(fs, ctx);
}

static int rename_touch_if(ffs *fs, struct rename_ctx *ctx, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = rename_touch(fs, ctx);
    return r;
}

static int rename_after(ffs *fs, struct rename_ctx *ctx)
{
    int r;

    rename_nlinks_if(fs, ctx, &r);
    r = rename_touch_if(fs, ctx, r);
    return r;
}

static int rename_after_if(ffs *fs, struct rename_ctx *ctx, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = rename_after(fs, ctx);
    return r;
}

static int rename_finish(ffs *fs, struct rename_ctx *ctx)
{
    int r;

    r = rename_insert(fs, ctx);
    r = rename_after_if(fs, ctx, r);
    return r;
}

static void remove_new_if(ffs *fs, struct rename_ctx *ctx, int *r)
{
    if (guard_skip(*r, ctx->texists == 0))
    {
        return;
    }
    *r = rename_remove_new(fs, ctx);
}

static int rename_finish_if(ffs *fs, struct rename_ctx *ctx, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = rename_finish(fs, ctx);
    return r;
}

static int rename_mutate(ffs *fs, struct rename_ctx *ctx)
{
    int r;

    r = dirent_delete(fs, ctx->op, ctx->oname);
    remove_new_if(fs, ctx, &r);
    r = rename_finish_if(fs, ctx, r);
    return r;
}

static int rename_ready(struct rename_ctx *ctx)
{
    if (ctx->same)
    {
        return 1;
    }
    ctx->t = now();
    return 0;
}

static int rename_mutate_if(ffs *fs, struct rename_ctx *ctx, int r)
{
    if (r != 0)
    {
        return 0;
    }
    r = rename_mutate(fs, ctx);
    return r;
}

static int rename_work(ffs *fs, void *v)
{
    struct rename_ctx *ctx;
    int r;

    ctx = (struct rename_ctx *)v;
    r = rename_ready(ctx);
    r = rename_mutate_if(fs, ctx, r);
    return r;
}

static int rename_txn(ffs *fs, struct rename_ctx *ctx, int r)
{
    if (r != 0)
    {
        return r;
    }
    r = txn_done(fs, rename_work, ctx);
    return r;
}

static int rename_run(ffs *fs, int olddirfd, const char *oldpath, int newdirfd,
                      const char *newpath, struct rename_ctx *ctx)
{
    int r;

    r = rename_prep(fs, olddirfd, oldpath, newdirfd, newpath, ctx);
    r = rename_txn(fs, ctx, r);
    r = fail_code(r);
    return r;
}

int ffs_renameat(ffs *fs, int olddirfd, const char *oldpath, int newdirfd,
                 const char *newpath)
{
    struct rename_ctx ctx = {0};
    int r;

    r = rename_run(fs, olddirfd, oldpath, newdirfd, newpath, &ctx);
    return r;
}

int ffs_rename(ffs *fs, const char *oldpath, const char *newpath)
{
    int r;

    r = ffs_renameat(fs, AT_FDCWD, oldpath, AT_FDCWD, newpath);
    return r;
}

static void stat_meta(struct ffs_stat *st, const struct finode *fi)
{
    st->st_mode = (unsigned long)fi->mode;
    st->st_uid = (unsigned long)fi->uid;
    st->st_gid = (unsigned long)fi->gid;
    st->st_ino = (unsigned long)fi->ino;
}

static void stat_attrs(struct ffs_stat *st, const struct finode *fi)
{
    st->st_size = (unsigned long)fi->size;
    st->st_atime = fi->atime;
    st->st_mtime = fi->mtime;
    st->st_ctime = fi->ctime;
}

static void stat_nlink(struct ffs_stat *st, const struct finode *fi)
{
    unsigned long n;

    n = (unsigned long)fi->nlink;
    n = n + (unsigned long)mode_is_dir(fi->mode);
    st->st_nlink = n;
}

static void stat_fill(struct ffs_stat *st, const struct finode *fi)
{
    stat_meta(st, fi);
    stat_nlink(st, fi);
    stat_attrs(st, fi);
}

static int stat_free(struct ffs_stat *st, struct finode *fi)
{
    stat_fill(st, fi);
    inode_free(fi);
    return 0;
}

static void stat_loaded(struct ffs_stat *st, struct finode *fi, int *r)
{
    if (*r < 0)
    {
        return; /* LCOV_EXCL_LINE */
    }
    *r = stat_free(st, fi);
}

int fill_stat(ffs *fs, long ino, struct ffs_stat *st)
{
    struct finode fi;
    int r;

    r = inode_load(fs, ino, &fi);
    stat_loaded(st, &fi, &r);
    return r;
}

static int fill_stat_ok(ffs *fs, long ino, struct ffs_stat *st)
{
    int r;

    r = fill_stat(fs, ino, st);
    r = fail_code(r);
    return r;
}

static int path_at(ffs *fs, int dirfd, const char *path, int flags, int last,
                   struct path_res *res)
{
    int r;

    r = resolve_at(fs, dirfd, path, nofollow(flags), last, &res->parent,
                   res->name, &res->ino);
    return r;
}

static void path_at_if(ffs *fs, int dirfd, const char *path, int flags,
                       int last, struct path_res *res, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = path_at(fs, dirfd, path, flags, last, res);
}

static int fstatat_prep(ffs *fs, int dirfd, const char *path,
                        struct ffs_stat *st, int flags, struct path_res *res)
{
    int r;

    r = choose(any_null3(fs, path, st), -EINVAL, 0);
    path_at_if(fs, dirfd, path, flags, 1, res, &r);
    return r;
}

static int stat_if(ffs *fs, struct ffs_stat *st, struct path_res *res, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = fill_stat_ok(fs, res->ino, st);
    return r;
}

static int fstatat_run(ffs *fs, int dirfd, const char *path,
                       struct ffs_stat *st, int flags, struct path_res *res)
{
    int r;

    r = fstatat_prep(fs, dirfd, path, st, flags, res);
    r = fail_code(r);
    r = stat_if(fs, st, res, r);
    return r;
}

int ffs_fstatat(ffs *fs, int dirfd, const char *path, struct ffs_stat *st,
                int flags)
{
    struct path_res res;
    int r;

    r = fstatat_run(fs, dirfd, path, st, flags, &res);
    return r;
}

int ffs_stat(ffs *fs, const char *path, struct ffs_stat *st)
{
    int r;

    r = ffs_fstatat(fs, AT_FDCWD, path, st, 0);
    return r;
}

int ffs_lstat(ffs *fs, const char *path, struct ffs_stat *st)
{
    int r;

    r = ffs_fstatat(fs, AT_FDCWD, path, st, AT_SYMLINK_NOFOLLOW);
    return r;
}

static int access_ok2(ffs *fs, long ino, int amode)
{
    int r;

    r = access_ok(fs, ino, amode);
    r = fail_code(r);
    return r;
}

static int faccessat_prep(ffs *fs, int dirfd, const char *path, int flags,
                          struct path_res *res)
{
    int r;

    r = choose(either_null(fs, path), -EINVAL, 0);
    path_at_if(fs, dirfd, path, flags, 0, res, &r);
    return r;
}

static int access_if(ffs *fs, struct path_res *res, int amode, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = access_ok2(fs, res->ino, amode);
    return r;
}

static int faccessat_run(ffs *fs, int dirfd, const char *path, int amode,
                         int flags, struct path_res *res)
{
    int r;

    r = faccessat_prep(fs, dirfd, path, flags, res);
    r = fail_code(r);
    r = access_if(fs, res, amode, r);
    return r;
}

int ffs_faccessat(ffs *fs, int dirfd, const char *path, int amode, int flags)
{
    struct path_res res;
    int r;

    r = faccessat_run(fs, dirfd, path, amode, flags, &res);
    return r;
}

int ffs_access(ffs *fs, const char *path, int amode)
{
    int r;

    r = ffs_faccessat(fs, AT_FDCWD, path, amode, 0);
    return r;
}

static int meta_save(ffs *fs, struct finode *fi)
{
    int r;

    r = inode_store(fs, fi);
    inode_free(fi);
    return r;
}

static int load_if(ffs *fs, long ino, struct finode *fi, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = inode_load(fs, ino, fi);
    return r;
}

static int chmod_owner(ffs *fs, long ino, struct finode *fi)
{
    int r;

    r = check_owner(fs, ino);
    r = load_if(fs, ino, fi, r);
    return r;
}

static void chmod_set(struct finode *fi, unsigned long mode)
{
    unsigned long bits;

    bits = (unsigned long)fi->mode & FFS_S_IFMT;
    fi->mode = (long)(bits | (mode & FFS_MODE_MASK_ALL));
    fi->ctime = now();
}

static int chmod_free(ffs *fs, struct finode *fi, unsigned long mode)
{
    int r;

    chmod_set(fi, mode);
    r = meta_save(fs, fi);
    return r;
}

static int chmod_fin_if(ffs *fs, struct finode *fi, unsigned long mode, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = chmod_free(fs, fi, mode);
    return r;
}

static int chmod_apply(ffs *fs, long ino, unsigned long mode)
{
    struct finode fi;
    int r;

    r = chmod_owner(fs, ino, &fi);
    r = chmod_fin_if(fs, &fi, mode, r);
    return r;
}

static int chmod_ok(ffs *fs, long ino, unsigned long mode)
{
    int r;

    r = chmod_apply(fs, ino, mode);
    r = fail_code(r);
    return r;
}

static int fchmodat_prep(ffs *fs, int dirfd, const char *path, int flags,
                         struct path_res *res)
{
    int r;

    r = choose(either_null(fs, path), -EINVAL, 0);
    path_at_if(fs, dirfd, path, flags, 0, res, &r);
    return r;
}

static int chmod_if(ffs *fs, struct path_res *res, unsigned long mode, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = chmod_ok(fs, res->ino, mode);
    return r;
}

static int fchmodat_run(ffs *fs, int dirfd, const char *path,
                        unsigned long mode, int flags, struct path_res *res)
{
    int r;

    r = fchmodat_prep(fs, dirfd, path, flags, res);
    r = fail_code(r);
    r = chmod_if(fs, res, mode, r);
    return r;
}

int ffs_fchmodat(ffs *fs, int dirfd, const char *path, unsigned long mode,
                 int flags)
{
    struct path_res res;
    int r;

    r = fchmodat_run(fs, dirfd, path, mode, flags, &res);
    return r;
}

int ffs_chmod(ffs *fs, const char *path, unsigned long mode)
{
    int r;

    r = ffs_fchmodat(fs, AT_FDCWD, path, mode, 0);
    return r;
}

static void chown_set(struct finode *fi, unsigned long uid, unsigned long gid)
{
    fi->uid = (long)uid;
    fi->gid = (long)gid;
    fi->ctime = now();
}

static int chown_free(ffs *fs, struct finode *fi, unsigned long uid,
                      unsigned long gid)
{
    int r;

    chown_set(fi, uid, gid);
    r = meta_save(fs, fi);
    return r;
}

static int chown_fin_if(ffs *fs, struct finode *fi, unsigned long uid,
                        unsigned long gid, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = chown_free(fs, fi, uid, gid);
    return r;
}

static int chown_apply(ffs *fs, long ino, unsigned long uid, unsigned long gid)
{
    struct finode fi;
    int r;

    r = chmod_owner(fs, ino, &fi);
    r = chown_fin_if(fs, &fi, uid, gid, r);
    return r;
}

static int chown_ok(ffs *fs, long ino, unsigned long uid, unsigned long gid)
{
    int r;

    r = chown_apply(fs, ino, uid, gid);
    r = fail_code(r);
    return r;
}

static int fchownat_prep(ffs *fs, int dirfd, const char *path, int flags,
                         struct path_res *res)
{
    int r;

    r = choose(either_null(fs, path), -EINVAL, 0);
    path_at_if(fs, dirfd, path, flags, 0, res, &r);
    return r;
}

static int chown_if(ffs *fs, struct path_res *res, unsigned long uid,
                    unsigned long gid, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = chown_ok(fs, res->ino, uid, gid);
    return r;
}

static int fchownat_run(ffs *fs, int dirfd, const char *path, unsigned long uid,
                        unsigned long gid, int flags, struct path_res *res)
{
    int r;

    r = fchownat_prep(fs, dirfd, path, flags, res);
    r = fail_code(r);
    r = chown_if(fs, res, uid, gid, r);
    return r;
}

int ffs_fchownat(ffs *fs, int dirfd, const char *path, unsigned long uid,
                 unsigned long gid, int flags)
{
    struct path_res res;
    int r;

    r = fchownat_run(fs, dirfd, path, uid, gid, flags, &res);
    return r;
}

int ffs_chown(ffs *fs, const char *path, unsigned long uid, unsigned long gid)
{
    int r;

    r = ffs_fchownat(fs, AT_FDCWD, path, uid, gid, 0);
    return r;
}

int ffs_lchown(ffs *fs, const char *path, unsigned long uid, unsigned long gid)
{
    int r;

    r = ffs_fchownat(fs, AT_FDCWD, path, uid, gid, AT_SYMLINK_NOFOLLOW);
    return r;
}

static int times_owner(ffs *fs, long ino, struct finode *fi)
{
    int r;

    r = check_times(fs, ino);
    r = load_if(fs, ino, fi, r);
    return r;
}

static void tv_times(struct finode *fi, const struct timeval tv[2])
{
    fi->atime = tv[0].tv_sec;
    fi->mtime = tv[1].tv_sec;
    fi->ctime = now();
}

static void now_times(struct finode *fi) /* LCOV_EXCL_LINE */
{
    long t;

    t = now();     /* LCOV_EXCL_LINE */
    fi->atime = t; /* LCOV_EXCL_LINE */
    fi->mtime = t; /* LCOV_EXCL_LINE */
    fi->ctime = t; /* LCOV_EXCL_LINE */
} /* LCOV_EXCL_LINE */

static void utimes_set(struct finode *fi, const struct timeval tv[2])
{
    if (tv)
    {
        tv_times(fi, tv);
        return;
    }
    now_times(fi); /* LCOV_EXCL_LINE */
}

static int utimes_err(ffs *fs, struct finode *fi, const struct timeval tv[2])
{
    int r;

    utimes_set(fi, tv);
    r = meta_save(fs, fi);
    r = fail_code(r);
    return r;
}

static int utimes_fin_if(ffs *fs, struct finode *fi, const struct timeval tv[2],
                         int r)
{
    if (r < 0)
    {
        return r;
    }
    r = utimes_err(fs, fi, tv);
    return r;
}

static int utimes_ok(ffs *fs, long ino, const struct timeval tv[2])
{
    struct finode fi;
    int r;

    r = times_owner(fs, ino, &fi);
    r = fail_code(r);
    r = utimes_fin_if(fs, &fi, tv, r);
    return r;
}

static int utimes_prep(ffs *fs, const char *path, struct path_res *res)
{
    int r;

    r = choose(either_null(fs, path), -EINVAL, 0);
    path_at_if(fs, AT_FDCWD, path, 0, 1, res, &r);
    return r;
}

static int utimes_path_if(ffs *fs, struct path_res *res,
                          const struct timeval tv[2], int r)
{
    if (r < 0)
    {
        return r;
    }
    r = utimes_ok(fs, res->ino, tv);
    return r;
}

static int utimes_run(ffs *fs, const char *path, const struct timeval tv[2])
{
    struct path_res res;
    int r;

    r = utimes_prep(fs, path, &res);
    r = fail_code(r);
    r = utimes_path_if(fs, &res, tv, r);
    return r;
}

int ffs_utimes(ffs *fs, const char *path, const struct timeval tv[2])
{
    int r;

    r = utimes_run(fs, path, tv);
    return r;
}

static int write_if(ffs *fs, struct path_res *res, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = check_write_file(fs, res->ino);
    return r;
}

static int truncate_prep(ffs *fs, const char *path, struct path_res *res)
{
    int r;

    r = choose(either_null(fs, path), -EINVAL, 0);
    path_at_if(fs, AT_FDCWD, path, 0, 1, res, &r);
    r = write_if(fs, res, r);
    return r;
}

static int truncate_ok(ffs *fs, long ino, ffs_off len)
{
    int r;

    r = truncate_ino(fs, ino, len);
    r = fail_code(r);
    return r;
}

static int truncate_if(ffs *fs, struct path_res *res, ffs_off len, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = truncate_ok(fs, res->ino, len);
    return r;
}

static int truncate_run(ffs *fs, const char *path, ffs_off len)
{
    struct path_res res;
    int r;

    r = truncate_prep(fs, path, &res);
    r = fail_code(r);
    r = truncate_if(fs, &res, len, r);
    return r;
}

int ffs_truncate(ffs *fs, const char *path, off_t len)
{
    int r;

    r = truncate_run(fs, path, (ffs_off)len);
    return r;
}

static int chdir_resolve(ffs *fs, const char *path, struct path_res *res)
{
    int r;

    r = choose(either_null(fs, path), -EINVAL, 0);
    path_at_if(fs, AT_FDCWD, path, 0, 1, res, &r);
    return r;
}

static int search_if(ffs *fs, struct path_res *res, int r)
{
    if (r != 0)
    {
        return r;
    }
    r = check_search(fs, res->ino);
    return r;
}

static int chdir_ok(ffs *fs, struct path_res *res)
{
    int r;

    r = is_dir(fs, res->ino);
    r = choose(r, 0, -ENOTDIR);
    r = search_if(fs, res, r);
    return r;
}

static int chdir_set(ffs *fs, struct path_res *res)
{
    fs->cwd = res->ino;
    return 0;
}

static int chdir_ok_if(ffs *fs, struct path_res *res, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = chdir_ok(fs, res);
    return r;
}

static int chdir_prep(ffs *fs, const char *path, struct path_res *res)
{
    int r;

    r = chdir_resolve(fs, path, res);
    r = chdir_ok_if(fs, res, r);
    return r;
}

static int chdir_set_if(ffs *fs, struct path_res *res, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = chdir_set(fs, res);
    return r;
}

int ffs_chdir(ffs *fs, const char *path)
{
    struct path_res res;
    int r;

    r = chdir_prep(fs, path, &res);
    r = fail_code(r);
    r = chdir_set_if(fs, &res, r);
    return r;
}

static int cwd_ok(ffs *fs, char *buf, size_t bufsz)
{
    int r;

    r = build_path(fs, fs->cwd, buf, bufsz);
    r = fail_code(r);
    return r;
}

static int cwd_if(ffs *fs, char *buf, size_t bufsz, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = cwd_ok(fs, buf, bufsz);
    return r;
}

static int cwd_run(ffs *fs, char *buf, size_t bufsz)
{
    int r;

    r = choose(either_null(fs, buf), -EINVAL, 0);
    r = fail_code(r);
    r = cwd_if(fs, buf, bufsz, r);
    return r;
}

int ffs_getcwd(ffs *fs, char *buf, size_t bufsz)
{
    int r;

    r = cwd_run(fs, buf, bufsz);
    return r;
}

static int realpath_ok(ffs *fs, long ino, char *buf, size_t bufsz)
{
    int r;

    r = build_path(fs, ino, buf, bufsz);
    r = fail_code(r);
    return r;
}

static int realpath_prep(ffs *fs, const char *path, char *buf,
                         struct path_res *res)
{
    int r;

    r = choose(any_null3(fs, path, buf), -EINVAL, 0);
    path_at_if(fs, AT_FDCWD, path, 0, 1, res, &r);
    return r;
}

static int realpath_if(ffs *fs, char *buf, size_t bufsz, struct path_res *res,
                       int r)
{
    if (r < 0)
    {
        return r;
    }
    r = realpath_ok(fs, res->ino, buf, bufsz);
    return r;
}

static int realpath_run(ffs *fs, const char *path, char *buf, size_t bufsz,
                        struct path_res *res)
{
    int r;

    r = realpath_prep(fs, path, buf, res);
    r = fail_code(r);
    r = realpath_if(fs, buf, bufsz, res, r);
    return r;
}

int ffs_realpath(ffs *fs, const char *path, char *buf, size_t bufsz)
{
    struct path_res res;
    int r;

    r = realpath_run(fs, path, buf, bufsz, &res);
    return r;
}

static void statfs_set(struct ffs_statfs *buf, long files, long blocks)
{
    buf->f_bsize = 1;
    buf->f_files = (unsigned long)files;
    buf->f_ffree = 0;
    buf->f_blocks = (unsigned long)blocks;
    buf->f_bfree = 0;
}

static int blocks_if(struct ffs_statfs *buf, long files, long blocks, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    statfs_set(buf, files, blocks);
    return 0;
}

static int statfs_blocks(ffs *fs, struct ffs_statfs *buf, long files)
{
    long blocks;
    int r;

    blocks = stat_count(fs, SQL_STATFS_BLOCKS);
    r = fail_code((int)blocks);
    r = blocks_if(buf, files, blocks, r);
    return r;
}

static int files_if(ffs *fs, struct ffs_statfs *buf, long files, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = statfs_blocks(fs, buf, files);
    return r;
}

static int statfs_fill(ffs *fs, struct ffs_statfs *buf)
{
    long files;
    int r;

    files = stat_count(fs, SQL_STATFS_FILES);
    r = fail_code((int)files);
    r = files_if(fs, buf, files, r);
    return r;
}

static int statfs_if(ffs *fs, struct ffs_statfs *buf, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = statfs_fill(fs, buf);
    return r;
}

static int statfs_run(ffs *fs, struct ffs_statfs *buf)
{
    int r;

    r = choose(either_null(fs, buf), -EINVAL, 0);
    r = fail_code(r);
    r = statfs_if(fs, buf, r);
    return r;
}

int ffs_statfs(ffs *fs, struct ffs_statfs *buf)
{
    int r;

    r = statfs_run(fs, buf);
    return r;
}
#define SQL_DIR_LIST "SELECT name FROM dirents WHERE parent = ? ORDER BY name"

static int list_if(ffs *fs, long ino, int r)
{
    if (r != 0)
    {
        return r;
    }
    r = check_list_dir(fs, ino);
    return r;
}

static int opendir_type(ffs *fs, long ino)
{
    int r;

    r = is_dir(fs, ino);
    r = choose(r, 0, -ENOTDIR);
    r = list_if(fs, ino, r);
    return r;
}

static int opendir_ino(ffs *fs, struct path_res *res, long *ino)
{
    int r;

    *ino = res->ino;
    r = opendir_type(fs, *ino);
    return r;
}

static int opendir_ino_if(ffs *fs, struct path_res *res, long *ino, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = opendir_ino(fs, res, ino);
    return r;
}

static int opendir_resolve(ffs *fs, const char *path, long *ino)
{
    struct path_res res;
    int r;

    r = resolve(fs, path, 1, &res.parent, res.name, &res.ino);
    r = opendir_ino_if(fs, &res, ino, r);
    return r;
}

static int opendir_resolve_if(ffs *fs, const char *path, long *ino, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = opendir_resolve(fs, path, ino);
    return r;
}

static int opendir_prep(ffs *fs, const char *path, long *ino)
{
    int r;

    r = choose(either_null(fs, path), -EINVAL, 0);
    r = opendir_resolve_if(fs, path, ino, r);
    return r;
}

static ffs_dir *dir_err(int r)
{
    errno = -r;
    return NULL;
}

static ffs_dir *dir_bad(ffs_dir *d) /* LCOV_EXCL_LINE */
{
    free(d); /* LCOV_EXCL_LINE */

    errno = EIO; /* LCOV_EXCL_LINE */
    return NULL; /* LCOV_EXCL_LINE */
}

static size_t dir_cap(long count)
{
    if (count < 1)
    {
        return 1;
    }
    return (size_t)count;
}

static int dir_names_ok(ffs_dir *d)
{
    size_t cap;

    cap = dir_cap(d->count);
    d->names = (char **)calloc(cap, sizeof(char *));
    return choose(d->names == NULL, -1, 0); /* LCOV_EXCL_LINE */
}

static int dir_names_if(ffs_dir *d, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = dir_names_ok(d);
    return r;
}

static int dir_count_ok(ffs_dir *d)
{
    long c;

    c = dir_count(d->fs, d->ino);
    d->count = c;
    return neg_code(c);
}

static void count_set_if(ffs_dir *d, int r, ffs_dir **out)
{
    if (r < 0)
    {
        *out = dir_bad(d); /* LCOV_EXCL_LINE */
        return;            /* LCOV_EXCL_LINE */
    }
    *out = d;
}

static ffs_dir *dir_count_set(ffs_dir *d)
{
    int r;
    ffs_dir *out;

    r = dir_count_ok(d);
    r = dir_names_if(d, r);
    count_set_if(d, r, &out);
    return out;
}

static ffs_dir *dir_init(ffs_dir *d, ffs *fs, long ino)
{
    ffs_dir *out;

    d->fs = fs;
    d->ino = ino;
    d->pos = FFS_DIR_POS_START;
    out = dir_count_set(d);
    return out;
}

static void dir_init_if(ffs_dir *d, ffs *fs, long ino, ffs_dir **out)
{
    if (d == NULL)
    {
        *out = NULL; /* LCOV_EXCL_LINE */
        return;      /* LCOV_EXCL_LINE */
    }
    *out = dir_init(d, fs, ino);
}

static ffs_dir *dir_alloc(ffs *fs, long ino)
{
    ffs_dir *d;
    ffs_dir *out;

    d = (ffs_dir *)calloc(1, sizeof(ffs_dir));
    dir_init_if(d, fs, ino, &out);
    return out;
}

static int keep_name(ffs_dir *d, int *n, char *name)
{
    if (name == NULL)
    {
        return -1; /* LCOV_EXCL_LINE */
    }
    d->names[*n] = name;
    *n = *n + 1;
    return 0;
}

static int dir_name_store(sqlite3_stmt *s, ffs_dir *d, int *n)
{
    const unsigned char *txt;
    char *name;
    int r;

    txt = sqlite3_column_text(s, 0);
    name = xstrdup((const char *)txt);
    r = keep_name(d, n, name);
    return r;
}

static int step_after(sqlite3_stmt *s, ffs_dir *d, int *n, int st)
{
    if (st != 0)
    {
        return st; /* LCOV_EXCL_LINE */
    }
    st = dir_name_store(s, d, n);
    return st;
}

static int dir_name_step(sqlite3_stmt *s, ffs_dir *d, int *n)
{
    int st;

    st = sqlite3_step(s);
    st = choose(st == SQLITE_ROW, 0, 1); /* LCOV_EXCL_LINE */
    st = step_after(s, d, n, st);
    return st;
}

static int dir_run(sqlite3_stmt *s, ffs_dir *d, int *n, int r)
{
    while ((*n < d->count) & (r == 0))
    {
        r = dir_name_step(s, d, n);
    }
    return r;
}

static int dir_loop_fin(sqlite3_stmt *s, ffs_dir *d)
{
    int n;

    n = 0;
    dir_run(s, d, &n, 0);
    sql_fin(s);
    return choose(n == d->count, 0, -1); /* LCOV_EXCL_LINE */
}

static int dir_loop_fin_if(sqlite3_stmt *s, ffs_dir *d, int r)
{
    if (r != 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = dir_loop_fin(s, d);
    return r;
}

static int dir_fill(ffs *fs, ffs_dir *d)
{
    sqlite3_stmt *s;
    int r;

    s = prepare(fs, SQL_DIR_LIST);
    r = choose(s == NULL, -1, 0); /* LCOV_EXCL_LINE */
    sql_bind_i64(s, 1, d->ino);
    r = dir_loop_fin_if(s, d, r);
    return r;
}

static void fill_ready(ffs_dir *d, int r, ffs_dir **out)
{
    if (r == 0)
    {
        *out = d;
        return;
    }
    *out = dir_bad(d); /* LCOV_EXCL_LINE */
}

static ffs_dir *dir_filled(ffs *fs, ffs_dir *d)
{
    ffs_dir *out;
    int r;

    r = dir_fill(fs, d);
    fill_ready(d, r, &out);
    return out;
}

static void make_if(ffs *fs, ffs_dir *d, ffs_dir **out)
{
    if (d == NULL)
    {
        *out = NULL; /* LCOV_EXCL_LINE */
        return;      /* LCOV_EXCL_LINE */
    }
    *out = dir_filled(fs, d);
}

static ffs_dir *dir_make(ffs *fs, long ino)
{
    ffs_dir *d;
    ffs_dir *out;

    d = dir_alloc(fs, ino);
    make_if(fs, d, &out);
    return out;
}

static void opendir_done(ffs *fs, long ino, int r, ffs_dir **out)
{
    if (r != 0)
    {
        *out = dir_err(r);
        return;
    }
    *out = dir_make(fs, ino);
}

ffs_dir *ffs_opendir(ffs *fs, const char *path)
{
    long ino;
    ffs_dir *d;
    int r;

    r = opendir_prep(fs, path, &ino);
    opendir_done(fs, ino, r, &d);
    return d;
}

static int pos_state(long pos, long count)
{
    int s;

    s = choose(pos < count, 3, 0);
    s = choose(pos == FFS_DIR_POS_DOT, 2, s);
    s = choose(pos == FFS_DIR_POS_START, 1, s);
    return s;
}

static long pos_next(long pos, int s)
{
    long next;

    next = choose_long(s == 3, pos + 1, pos);
    next = choose_long(s == 2, 0, next);
    next = choose_long(s == 1, FFS_DIR_POS_DOT, next);
    return next;
}

static int dir_state(ffs_dir *d)
{
    int s;

    s = pos_state(d->pos, d->count);
    d->pos = pos_next(d->pos, s);
    return s;
}

static const char *dir_src_of(int state)
{
    if (state == 1)
    {
        return ".";
    }
    return choose_ptr(state == 2, "..", NULL);
}

static const char *dir_src(ffs_dir *d, int state)
{
    const char *src;

    src = dir_src_of(state);
    if (src != NULL)
    {
        return src;
    }
    return d->names[d->pos - 1];
}

static int dir_set_src(ffs_dir *d, const char **src, int s)
{
    if (s == 0)
    {
        return 0;
    }
    *src = dir_src(d, s);
    return 1;
}

static int dir_next(ffs_dir *d, const char **src)
{
    int s;

    s = dir_state(d);
    s = dir_set_src(d, src, s);
    return s;
}

static int copy_if(char *name, const char *src, size_t n, int r)
{
    if (r < 0)
    {
        return r;
    }
    memcpy(name, src, n);
    return 1;
}

static int dir_copy(char *name, size_t namesz, const char *src)
{
    size_t n;
    int r;

    n = strlen(src) + 1;
    r = choose(n > namesz, -ENAMETOOLONG, 0);
    r = fail_code(r);
    r = copy_if(name, src, n, r);
    return r;
}

static void readdir_next_if(ffs_dir *d, const char **src, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = dir_next(d, src);
}

static int readdir_copy_if(char *name, size_t namesz, const char *src, int r)
{
    if (r <= 0)
    {
        return r;
    }
    r = dir_copy(name, namesz, src);
    return r;
}

int ffs_readdir(ffs_dir *d, char *name, size_t namesz)
{
    const char *src;
    int r;

    r = choose(either_null(d, name), -EINVAL, 0);
    r = fail_code(r);
    readdir_next_if(d, &src, &r);
    r = readdir_copy_if(name, namesz, src, r);
    return r;
}

static void dir_free_names(ffs_dir *d)
{
    long i;

    for (i = 0; i < d->count; ++i)
    {
        free(d->names[i]);
    }
}

static void dir_free_all(ffs_dir *d)
{
    dir_free_names(d);
    free(d->names);
    free(d);
}

static int close_dir_if(ffs_dir *d, int r)
{
    if (r < 0)
    {
        return r;
    }
    dir_free_all(d);
    return 0;
}

int ffs_closedir(ffs_dir *d)
{
    int r;

    r = choose(d == NULL, -EINVAL, 0);
    r = fail_code(r);
    r = close_dir_if(d, r);
    return r;
}

int open_follow(int flags)
{
    int r;

    r = choose(flags & O_NOFOLLOW, 0, 1);
    r = choose((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL), 0, r);
    return r;
}

static void open_flags(struct open_ctx *ctx, int flags, unsigned long mode)
{
    ctx->create = choose(flags & O_CREAT, 1, 0);
    ctx->dirflag = choose(flags & O_DIRECTORY, 1, 0);
    ctx->follow = open_follow(flags);
    ctx->flags = flags;
    ctx->mode = mode;
}

static void open_flags_if(struct open_ctx *ctx, int flags, unsigned long mode,
                          int r)
{
    if (r < 0)
    {
        return;
    }
    open_flags(ctx, flags, mode);
}

static int open_flags_set(ffs *fs, const char *path, int flags,
                          unsigned long mode, struct open_ctx *ctx)
{
    int r;

    r = choose(either_null(fs, path), -EINVAL, 0);
    open_flags_if(ctx, flags, mode, r);
    return r;
}

static int open_resolve(ffs *fs, int dirfd, const char *path,
                        struct open_ctx *ctx)
{
    int r;

    r = resolve_at(fs, dirfd, path, ctx->follow, 0, &ctx->parent, ctx->name,
                   &ctx->ino);
    return r;
}

static int is_symlink_ino(ffs *fs, long ino)
{
    long mode;
    int r;

    mode = 0;
    r = inode_mode(fs, ino, &mode);
    r = choose(r < 0, 0, symlink_mode(mode));
    return r;
}

static int open_dir_check_if(ffs *fs, struct open_ctx *ctx, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = check_read_file(fs, ctx->ino);
    return r;
}

static int open_dir(ffs *fs, struct open_ctx *ctx)
{
    int r;

    r = choose(ctx->dirflag, 0, -EISDIR);
    r = open_dir_check_if(fs, ctx, r);
    return r;
}

int open_type_code(int d, int dirflag)
{
    int r;

    r = choose(d, choose(dirflag, 0, -EISDIR), choose(dirflag, -ENOTDIR, 0));
    return r;
}

static int open_type_dir_if(ffs *fs, struct open_ctx *ctx, int r)
{
    if (guard_skip(r, !ctx->isdir))
    {
        return r;
    }
    r = open_dir(fs, ctx);
    return r;
}

static int open_type_reg_if(ffs *fs, struct open_ctx *ctx, int r)
{
    if (guard_skip(r, ctx->isdir))
    {
        return r;
    }
    r = check_access(fs, ctx->ino, ctx->flags);
    return r;
}

static int open_type_check(ffs *fs, struct open_ctx *ctx)
{
    int r;

    r = open_type_code(ctx->isdir, ctx->dirflag);
    r = open_type_dir_if(fs, ctx, r);
    r = open_type_reg_if(fs, ctx, r);
    return r;
}

static int open_type(ffs *fs, struct open_ctx *ctx)
{
    int r;

    ctx->isdir = is_dir(fs, ctx->ino);
    r = open_type_check(fs, ctx);
    return r;
}

static int open_nofollow_if(ffs *fs, struct open_ctx *ctx, int r)
{
    if (!r)
    {
        return 0;
    }
    r = is_symlink_ino(fs, ctx->ino);
    return choose(r, -ELOOP, 0);
}

static int open_nofollow_code(ffs *fs, struct open_ctx *ctx)
{
    int r;

    r = choose(ctx->flags & O_NOFOLLOW, 1, 0);
    r = open_nofollow_if(fs, ctx, r);
    return r;
}

int open_excl_code(int create, int flags)
{
    int excl;

    excl = choose(flags & O_EXCL, 1, 0);
    excl = choose(create, excl, 0);
    return choose(excl, -EEXIST, 0);
}

static int open_existing_type_if(ffs *fs, struct open_ctx *ctx, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = open_type(fs, ctx);
    return r;
}

static int open_existing(ffs *fs, struct open_ctx *ctx)
{
    int r;

    r = open_nofollow_code(fs, ctx);
    r = choose(r < 0, r, open_excl_code(ctx->create, ctx->flags));
    r = open_existing_type_if(fs, ctx, r);
    return r;
}

static int path_ends_slash(const char *path)
{
    if (strlen(path) == 0)
    {
        return 0;
    }
    return path[strlen(path) - 1] == '/';
}

int open_new_bad(int dirflag, const char *name, const char *path)
{
    int r;

    r = choose(dirflag, -EINVAL, choose(name_empty(name), -ENOENT, 0));
    r = choose(r < 0, r, choose(path_ends_slash(path), -EISDIR, 0));
    return r;
}

static int open_new_ok(ffs *fs, struct open_ctx *ctx)
{
    int r;

    r = open_new_bad(ctx->dirflag, ctx->name, ctx->path);
    r = check_create_if(fs, ctx->parent, r);
    return r;
}

static int open_create_ok(ffs *fs, struct open_ctx *ctx)
{
    int r;

    ctx->made = 1;
    r = open_new_ok(fs, ctx);
    return r;
}

int open_retry(int r, int create)
{
    int c;

    c = choose(r == -ENOENT, 1, 0);
    c = choose(create, c, 0);
    return c;
}

static int open_after_existing_if(ffs *fs, struct open_ctx *ctx, int r)
{
    if (r != 0)
    {
        return r;
    }
    r = open_existing(fs, ctx);
    return r;
}

static int open_after_create_if(ffs *fs, struct open_ctx *ctx, int r)
{
    if (!open_retry(r, ctx->create))
    {
        return r;
    }
    r = open_create_ok(fs, ctx);
    return r;
}

static int open_after(ffs *fs, int dirfd, struct open_ctx *ctx)
{
    int r;

    r = open_resolve(fs, dirfd, ctx->path, ctx);
    r = open_after_existing_if(fs, ctx, r);
    r = open_after_create_if(fs, ctx, r);
    return r;
}

static int open_after_if(ffs *fs, int dirfd, struct open_ctx *ctx, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = open_after(fs, dirfd, ctx);
    return r;
}

static int open_validate(ffs *fs, int dirfd, const char *path, int flags,
                         unsigned long mode, struct open_ctx *ctx)
{
    int r;

    ctx->path = path;
    r = open_flags_set(fs, path, flags, mode, ctx);
    r = open_after_if(fs, dirfd, ctx, r);
    return r;
}

static struct finode open_finode(ffs *fs, struct open_ctx *ctx)
{
    struct finode fi;
    long mode;

    mode = masked_mode(fs, FFS_S_IFREG, ctx->mode);
    fi = finode_new(fs, ctx->parent, mode);
    fi_times(&fi, ctx->t);
    return fi;
}

static long open_new_inode(ffs *fs, struct open_ctx *ctx)
{
    struct finode fi;
    long ino;

    fi = open_finode(fs, ctx);
    ino = inode_new(fs, &fi);
    ctx->ino = ino;
    return ino;
}

static int open_new_dirents(ffs *fs, struct open_ctx *ctx, long ino)
{
    int r;

    r = dirent_insert(fs, ctx->parent, ctx->name, ino);
    r = touch_if(fs, ctx->parent, ctx->t, r);
    return r;
}

static int open_dirents_if(ffs *fs, struct open_ctx *ctx, long ino, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = open_new_dirents(fs, ctx, ino);
    return r;
}

static int open_create_add(ffs *fs, struct open_ctx *ctx)
{
    long ino;
    int r;

    ino = open_new_inode(fs, ctx);
    r = neg_code(ino);
    r = open_dirents_if(fs, ctx, ino, r);
    return r;
}

static int open_create_work(ffs *fs, void *v)
{
    struct open_ctx *ctx;
    int r;

    ctx = (struct open_ctx *)v;
    ctx->t = now();
    r = open_create_add(fs, ctx);
    return r;
}

static int open_alloc_of(ffs *fs, struct open_ctx *ctx)
{
    int d;
    int r;

    d = is_dir(fs, ctx->ino);
    r = of_alloc(fs, ctx->ino, ctx->flags, d != 0);
    return r;
}

static void open_fd_fail(ffs *fs, int of) /* LCOV_EXCL_LINE */
{
    fs->ofs[of].used = 0; /* LCOV_EXCL_LINE */
}

static void open_fd_fail_if(ffs *fs, int of, int fd)
{
    if (fd < 0)
    {
        open_fd_fail(fs, of); /* LCOV_EXCL_LINE */
    }
}

static int open_fd(ffs *fs, int of)
{
    int fd;

    fd = fd_alloc2(fs, of, 0);
    open_fd_fail_if(fs, of, fd);
    return fd;
}

static void trunc_cleanup(ffs *fs, int fd, int of) /* LCOV_EXCL_LINE */
{
    fs->fds[fd].used = 0; /* LCOV_EXCL_LINE */
    of_release(fs, of);   /* LCOV_EXCL_LINE */
} /* LCOV_EXCL_LINE */

static void trunc_cleanup_if(ffs *fs, int fd, int of, int r)
{
    if (r < 0)
    {
        trunc_cleanup(fs, fd, of); /* LCOV_EXCL_LINE */
    }
}

static int trunc_fail(ffs *fs, long ino, int fd, int of)
{
    int r;

    r = truncate_ino(fs, ino, 0);
    trunc_cleanup_if(fs, fd, of, r);
    return r;
}

static int trunc_req_if(ffs *fs, struct open_ctx *ctx, int r)
{
    if (!r)
    {
        return 0;
    }
    r = trunc_fail(fs, ctx->ino, ctx->fd, ctx->of);
    return r;
}

static int open_trunc(ffs *fs, struct open_ctx *ctx)
{
    int r;

    r = choose(ctx->flags & O_TRUNC, 1, 0);
    r = trunc_req_if(fs, ctx, r);
    return r;
}

static int open_trunc_if(ffs *fs, struct open_ctx *ctx, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = open_trunc(fs, ctx);
    return choose(r < 0, r, ctx->fd);
}

static int open_fd_ok(ffs *fs, struct open_ctx *ctx)
{
    int r;

    ctx->fd = open_fd(fs, ctx->of);
    r = choose(ctx->fd < 0, ctx->fd, 0);
    r = open_trunc_if(fs, ctx, r);
    return r;
}

static int open_fd_if(ffs *fs, struct open_ctx *ctx, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = open_fd_ok(fs, ctx);
    return r;
}

static int open_bind(ffs *fs, struct open_ctx *ctx)
{
    int r;

    ctx->of = open_alloc_of(fs, ctx);
    r = choose(ctx->of < 0, ctx->of, 0);
    r = open_fd_if(fs, ctx, r);
    return r;
}

static int bind_ok(ffs *fs, struct open_ctx *ctx)
{
    int r;

    r = open_bind(fs, ctx);
    r = fail_code(r);
    return r;
}

static int open_txn_if(ffs *fs, struct open_ctx *ctx, int r)
{
    if (guard_skip(r, !ctx->made))
    {
        return r;
    }
    r = txn_done(fs, open_create_work, ctx);
    return r;
}

static int open_bind_if(ffs *fs, struct open_ctx *ctx, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = bind_ok(fs, ctx);
    return r;
}

static int open_finish(ffs *fs, struct open_ctx *ctx)
{
    int r;

    r = open_txn_if(fs, ctx, 0);
    r = open_bind_if(fs, ctx, r);
    return r;
}

static int open_validate_ok(ffs *fs, int dirfd, const char *path, int flags,
                            unsigned long mode, struct open_ctx *ctx)
{
    int r;

    r = open_validate(fs, dirfd, path, flags, mode, ctx);
    r = fail_code(r);
    return r;
}

static int open_finish_if(ffs *fs, struct open_ctx *ctx, int r)
{
    if (r != 0)
    {
        return r;
    }
    r = open_finish(fs, ctx);
    return r;
}

int ffs_openat(ffs *fs, int dirfd, const char *path, int flags,
               unsigned long mode)
{
    struct open_ctx ctx;
    int r;

    memset(&ctx, 0, sizeof(ctx));
    r = open_validate_ok(fs, dirfd, path, flags, mode, &ctx);
    r = open_finish_if(fs, &ctx, r);
    return r;
}

int ffs_open(ffs *fs, const char *path, int flags, unsigned long mode)
{
    int r;

    r = ffs_openat(fs, AT_FDCWD, path, flags, mode);
    return r;
}

static int refs_left(ffs *fs, int of)
{
    return fs->ofs[of].refs - 1;
}

int unlinked_unused(ffs *fs, long ino, long nlink)
{
    int u;

    u = choose(nlink == 0, 1, 0);
    u = choose(ino_fd_refs(fs, ino) == 0, u, 0);
    return u;
}

static void release_delete_if(ffs *fs, long ino, long nlink)
{
    int u;

    u = unlinked_unused(fs, ino, nlink);
    if (u)
    {
        inode_delete(fs, ino);
    }
}

static void release_free(ffs *fs, struct finode *fi, long ino)
{
    release_delete_if(fs, ino, fi->nlink);
    inode_free(fi);
}

static void release_loaded_if(ffs *fs, struct finode *fi, long ino, int r)
{
    if (r < 0)
    {
        return; /* LCOV_EXCL_LINE */
    }
    release_free(fs, fi, ino);
}

static void release_load(ffs *fs, long ino)
{
    struct finode fi;
    int r;

    r = inode_load(fs, ino, &fi);
    release_loaded_if(fs, &fi, ino, r);
}

static void release_ino(ffs *fs, int of)
{
    long ino;

    ino = fs->ofs[of].ino;
    fs->ofs[of].used = 0;
    release_load(fs, ino);
}

static void of_release(ffs *fs, int of)
{
    fs->ofs[of].refs = refs_left(fs, of);
    if (fs->ofs[of].refs > 0)
    {
        return;
    }
    release_ino(fs, of);
}

static void close_of(ffs *fs, int fd, int of)
{
    fs->fds[fd].used = 0;
    of_release(fs, of);
}

static int close_of_if(ffs *fs, int fd, int *of, int r)
{
    if (r < 0)
    {
        return r;
    }
    close_of(fs, fd, *of);
    return 0;
}

int ffs_close_fd(ffs *fs, int fd)
{
    int of;
    int r;

    r = fd_of(fs, fd, &of);
    r = fail_code(r);
    r = close_of_if(fs, fd, &of, r);
    return r;
}

struct rw_ctx
{
    int of;
    ffs_off pos;
    size_t want;
    size_t n;
    void *buf;
    off_t off;
    ssize_t r;
};

struct wctx
{
    int of;
    ffs_off pos;
    ffs_off end;
    const void *buf;
    size_t n;
};

int rw_read_code(int is_dir, int flags)
{
    int r;

    r = choose((flags & O_ACCMODE) == O_WRONLY, -EBADF, 0);
    r = choose(is_dir, -EISDIR, r);
    return r;
}

int rw_write_code(int is_dir, int flags)
{
    int r;

    r = choose((flags & O_ACCMODE) == O_RDONLY, -EBADF, 0);
    r = choose(is_dir, -EISDIR, r);
    return r;
}

static int read_fd_if(ffs *fs, int fd, struct rw_ctx *c, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = fd_of(fs, fd, &c->of);
    return r;
}

static int read_code_if(ffs *fs, struct rw_ctx *c, int r)
{
    if (r < 0)
    {
        return r;
    }
    return rw_read_code(fs->ofs[c->of].is_dir, fs->ofs[c->of].flags);
}

static int read_check_if(ffs *fs, int fd, struct rw_ctx *c, int r)
{
    r = read_fd_if(fs, fd, c, r);
    r = read_code_if(fs, c, r);
    return r;
}

static int read_check(ffs *fs, int fd, struct rw_ctx *c)
{
    int r;

    r = choose(either_null(fs, c->buf), -EINVAL, 0);
    r = read_check_if(fs, fd, c, r);
    return r;
}

static int read_load(ffs *fs, struct rw_ctx *c, struct finode *fi)
{
    int r;

    r = inode_load(fs, fs->ofs[c->of].ino, fi);
    c->pos = fs->ofs[c->of].offset;
    return choose(r < 0, r, 0);
}

static int read_load_if(ffs *fs, struct rw_ctx *c, struct finode *fi, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = read_load(fs, c, fi);
    return r;
}

static int read_prep(ffs *fs, int fd, struct rw_ctx *c, struct finode *fi)
{
    int r;

    r = read_check(fs, fd, c);
    r = read_load_if(fs, c, fi, r);
    return r;
}

size_t read_want(const struct finode *fi, ffs_off pos, size_t n)
{
    if (pos >= fi->size)
    {
        return 0;
    }
    return min_size(n, (size_t)(fi->size - pos));
}

static void touch_atime(ffs *fs, struct finode *fi)
{
    fi->atime = now();
    inode_store(fs, fi);
}

static int read_data(ffs *fs, struct rw_ctx *c)
{
    ssize_t got;

    got = data_read(fs, fs->ofs[c->of].ino, c->buf, c->want, c->pos);
    return choose(got == (ssize_t)c->want, 0, -EIO);
}

static void read_commit(ffs *fs, struct rw_ctx *c, struct finode *fi)
{
    read_data(fs, c);
    if (fs->atime_on)
    {
        touch_atime(fs, fi);
    }
    fs->ofs[c->of].offset = c->pos + (ffs_off)c->want;
}

static ssize_t read_commit_if(ffs *fs, struct rw_ctx *c, struct finode *fi)
{
    if (c->want == 0)
    {
        return 0;
    }
    read_commit(fs, c, fi);
    return (ssize_t)c->want;
}

static ssize_t read_finish(ffs *fs, struct rw_ctx *c, struct finode *fi)
{
    ssize_t out;

    c->want = read_want(fi, c->pos, c->n);
    out = read_commit_if(fs, c, fi);
    inode_free(fi);
    return out;
}

static ssize_t read_prep_ok(ffs *fs, int fd, struct rw_ctx *c,
                            struct finode *fi)
{
    int r;

    r = read_prep(fs, fd, c, fi);
    r = fail_code(r);
    return (ssize_t)r;
}

static ssize_t read_finish_if(ffs *fs, struct rw_ctx *c, struct finode *fi)
{
    if (guard_skip((int)c->r, 0))
    {
        return c->r;
    }
    c->r = read_finish(fs, c, fi);
    return c->r;
}

static ssize_t read_body(ffs *fs, int fd, struct rw_ctx *c)
{
    struct finode fi;

    c->r = read_prep_ok(fs, fd, c, &fi);
    c->r = read_finish_if(fs, c, &fi);
    return c->r;
}

static void rw_set(struct rw_ctx *c, void *buf, size_t n, off_t off)
{
    c->buf = buf;
    c->n = n;
    c->off = off;
}

ssize_t ffs_read(ffs *fs, int fd, void *buf, size_t n)
{
    struct rw_ctx c;
    ssize_t out;

    rw_set(&c, buf, n, 0);
    out = read_body(fs, fd, &c);
    return out;
}

static int write_fd_if(ffs *fs, int fd, struct wctx *c, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = fd_of(fs, fd, &c->of);
    return r;
}

static int write_code_if(ffs *fs, struct wctx *c, int r)
{
    if (r < 0)
    {
        return r;
    }
    return rw_write_code(fs->ofs[c->of].is_dir, fs->ofs[c->of].flags);
}

static int write_check_if(ffs *fs, int fd, struct wctx *c, int r)
{
    r = write_fd_if(fs, fd, c, r);
    r = write_code_if(fs, c, r);
    return r;
}

static int write_check(ffs *fs, int fd, const void *buf, struct wctx *c)
{
    int r;

    r = choose(either_null(fs, buf), -EINVAL, 0);
    r = write_check_if(fs, fd, c, r);
    return r;
}

static int write_load(ffs *fs, struct wctx *c, struct finode *fi)
{
    int r;

    r = inode_load(fs, fs->ofs[c->of].ino, fi);
    return r;
}

static ffs_off write_pos(ffs *fs, struct wctx *c, const struct finode *fi)
{
    if (fs->ofs[c->of].flags & O_APPEND)
    {
        return fi->size;
    }
    return fs->ofs[c->of].offset;
}

int write_end_bad(ffs_off pos, ffs_off d)
{
    int bad;

    bad = choose(d < 0, 1, 0);
    bad = choose(pos + d < pos, 1, bad);
    return bad;
}

static int write_end(struct wctx *c, size_t n)
{
    ffs_off d;

    d = (ffs_off)n;
    c->end = c->pos + d;
    return choose(write_end_bad(c->pos, d), -EFBIG, 0);
}

static void write_grow(struct finode *fi, ffs_off end)
{
    if (end > fi->size)
    {
        fi->size = end;
    }
    fi->mtime = now();
    fi->ctime = now();
}

static int write_grow_free(ffs *fs, struct finode *fi, ffs_off end)
{
    int r;

    write_grow(fi, end);
    r = inode_store(fs, fi);
    inode_free(fi);
    return r;
}

static int write_size_if(ffs *fs, struct finode *fi, ffs_off end, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = write_grow_free(fs, fi, end);
    return r;
}

static int write_size(ffs *fs, struct wctx *c)
{
    struct finode fi;
    int r;

    r = inode_load(fs, fs->ofs[c->of].ino, &fi);
    r = write_size_if(fs, &fi, c->end, r);
    return r;
}

static int write_size_wrap(ffs *fs, struct wctx *c, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = write_size(fs, c);
    return r;
}

static int write_pages(ffs *fs, void *v)
{
    struct wctx *c;
    int r;

    c = (struct wctx *)v;
    r = data_write(fs, fs->ofs[c->of].ino, c->buf, c->n, c->pos);
    r = write_size_wrap(fs, c, r);
    return r;
}

static int write_load_pos(ffs *fs, struct wctx *c, struct finode *fi, int r)
{
    c->pos = write_pos(fs, c, fi);
    inode_free(fi);
    return r;
}

static int write_prep_pos(ffs *fs, struct wctx *c)
{
    struct finode fi;
    int r;

    r = write_load(fs, c, &fi);
    r = write_load_pos(fs, c, &fi, r);
    return r;
}

static int write_end_if(struct wctx *c, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = write_end(c, c->n);
    return r;
}

static int write_prep(ffs *fs, struct wctx *c)
{
    int r;

    r = write_prep_pos(fs, c);
    r = write_end_if(c, r);
    return r;
}

static ssize_t write_prep_ok(ffs *fs, struct wctx *c)
{
    int r;

    r = write_prep(fs, c);
    r = fail_code(r);
    return (ssize_t)r;
}

static int write_txn_run(ffs *fs, struct wctx *c)
{
    int r;

    r = txn_run(fs, write_pages, c);
    r = fail_code(r);
    return r;
}

static ssize_t write_txn_ok(ffs *fs, struct wctx *c, ssize_t out)
{
    if (guard_skip((int)out, 0))
    {
        return out;
    }
    out = (ssize_t)write_txn_run(fs, c);
    return out;
}

static ssize_t write_off_if(ffs *fs, struct wctx *c, ssize_t out)
{
    if (out < 0)
    {
        return out;
    }
    fs->ofs[c->of].offset = c->end;
    return (ssize_t)c->n;
}

static ssize_t write_body(ffs *fs, struct wctx *c)
{
    ssize_t out;

    out = write_prep_ok(fs, c);
    out = write_txn_ok(fs, c, out);
    out = write_off_if(fs, c, out);
    return out;
}

static ssize_t write_body_if(ffs *fs, struct wctx *c, ssize_t out)
{
    if (guard_skip((int)out, c->n == 0))
    {
        return out;
    }
    out = write_body(fs, c);
    return out;
}

static void write_set(struct wctx *c, const void *buf, size_t n)
{
    c->buf = buf;
    c->n = n;
}

static ssize_t write_checked(ffs *fs, struct wctx *c, int r, const void *buf,
                             size_t n)
{
    ssize_t out;

    write_set(c, buf, n);
    out = (ssize_t)fail_code(r);
    out = write_body_if(fs, c, out);
    return out;
}

ssize_t ffs_write(ffs *fs, int fd, const void *buf, size_t n)
{
    struct wctx c;
    int r;
    ssize_t out;

    r = write_check(fs, fd, buf, &c);
    out = write_checked(fs, &c, r, buf, n);
    return out;
}

struct lseek_ctx
{
    int of;
    ffs_off base;
};

int seek_valid(int whence)
{
    int r;

    r = choose(whence == SEEK_SET, 0, -EINVAL);
    r = choose(whence == SEEK_CUR, 0, r);
    r = choose(whence == SEEK_END, 0, r);
    return r;
}

static int seek_set_if(struct lseek_ctx *c, int whence)
{
    if (guard_skip(seek_valid(whence), whence != SEEK_SET))
    {
        return seek_valid(whence);
    }
    c->base = 0;
    return 0;
}

static int seek_cur_if(ffs *fs, struct lseek_ctx *c, int whence, int r)
{
    if (guard_skip(r, whence != SEEK_CUR))
    {
        return r;
    }
    c->base = fs->ofs[c->of].offset;
    return 0;
}

static int fin_free(struct finode *fi)
{
    inode_free(fi);
    return 0;
}

static int end_base(struct lseek_ctx *c, struct finode *fi)
{
    int r;

    c->base = fi->size;
    r = fin_free(fi);
    return r;
}

static int end_base_if(struct lseek_ctx *c, struct finode *fi, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = end_base(c, fi);
    return r;
}

static int seek_end(ffs *fs, struct lseek_ctx *c)
{
    struct finode fi;
    int r;

    r = inode_load(fs, fs->ofs[c->of].ino, &fi);
    r = end_base_if(c, &fi, r);
    return r;
}

static int seek_end_if(ffs *fs, struct lseek_ctx *c, int whence, int r)
{
    if (guard_skip(r, whence != SEEK_END))
    {
        return r;
    }
    r = seek_end(fs, c);
    return r;
}

static int seek_base(ffs *fs, struct lseek_ctx *c, int whence)
{
    int r;

    r = seek_set_if(c, whence);
    r = seek_cur_if(fs, c, whence, r);
    r = seek_end_if(fs, c, whence, r);
    return r;
}

static int lseek_fd_if(ffs *fs, int fd, struct lseek_ctx *c, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = fd_of(fs, fd, &c->of);
    return r;
}

static int lseek_base_if(ffs *fs, int whence, struct lseek_ctx *c, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = seek_base(fs, c, whence);
    return r;
}

static int lseek_prep_ok(ffs *fs, int fd, int whence, struct lseek_ctx *c)
{
    int r;

    r = choose(fs == NULL, -EINVAL, 0);
    r = lseek_fd_if(fs, fd, c, r);
    r = lseek_base_if(fs, whence, c, r);
    r = fail_code(r);
    return r;
}

static off_t lseek_fail_if(off_t bad)
{
    off_t out;

    if (!bad)
    {
        return 0;
    }
    out = (off_t)util_fail(EINVAL);
    return out;
}

static off_t lseek_set_off(ffs *fs, struct lseek_ctx *c, ffs_off newoff)
{
    fs->ofs[c->of].offset = newoff;
    return (off_t)newoff;
}

static off_t lseek_ok_if(ffs *fs, struct lseek_ctx *c, ffs_off n, off_t out)
{
    if (out != 0)
    {
        return out;
    }
    out = lseek_set_off(fs, c, n);
    return out;
}

static off_t lseek_apply(ffs *fs, struct lseek_ctx *c, off_t off)
{
    ffs_off newoff;
    off_t out;

    newoff = c->base + (ffs_off)off;
    out = lseek_fail_if(choose(newoff < 0, 1, 0));
    out = lseek_ok_if(fs, c, newoff, out);
    return out;
}

static off_t lseek_run(ffs *fs, struct lseek_ctx *c, off_t off, off_t out)
{
    if (out < 0)
    {
        return out;
    }
    out = lseek_apply(fs, c, off);
    return out;
}

off_t ffs_lseek(ffs *fs, int fd, off_t off, int whence)
{
    struct lseek_ctx c;
    int r;
    off_t out;

    r = lseek_prep_ok(fs, fd, whence, &c);
    out = lseek_run(fs, &c, off, r);
    return out;
}

int rw_trunc_code(int is_dir, int flags)
{
    int r;

    r = choose((flags & O_ACCMODE) == O_RDONLY, -EINVAL, 0);
    r = choose(is_dir, -EISDIR, r);
    return r;
}

static int ftruncate_ok(ffs *fs, int of)
{
    return rw_trunc_code(fs->ofs[of].is_dir, fs->ofs[of].flags);
}

static int ftruncate_code_if(ffs *fs, int *of, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = ftruncate_ok(fs, *of);
    return r;
}

static int ftruncate_prep(ffs *fs, int fd, int *of)
{
    int r;

    r = fd_of(fs, fd, of);
    r = ftruncate_code_if(fs, of, r);
    return r;
}

static int ftruncate_run_if(ffs *fs, int *of, off_t len, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = truncate_ok(fs, fs->ofs[*of].ino, (ffs_off)len);
    return r;
}

int ffs_ftruncate(ffs *fs, int fd, off_t len)
{
    int of;
    int r;

    r = ftruncate_prep(fs, fd, &of);
    r = fail_code(r);
    r = ftruncate_run_if(fs, &of, len, r);
    return r;
}

static int fstat_fd_if(ffs *fs, int fd, int *of, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = fd_of(fs, fd, of);
    return r;
}

static int fstat_fd(ffs *fs, int fd, int *of, struct ffs_stat *st)
{
    int r;

    r = choose(either_null(fs, st), -EBADF, 0);
    r = fstat_fd_if(fs, fd, of, r);
    return r;
}

static int fstat_fill_if(ffs *fs, int *of, struct ffs_stat *st, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = fill_stat_ok(fs, fs->ofs[*of].ino, st);
    return r;
}

int ffs_fstat(ffs *fs, int fd, struct ffs_stat *st)
{
    int of;
    int r;

    r = fstat_fd(fs, fd, &of, st);
    r = fail_code(r);
    r = fstat_fill_if(fs, &of, st, r);
    return r;
}

static int fd_of2(ffs *fs, int fd, int *of)
{
    int r;

    r = fd_of(fs, fd, of);
    return r;
}

static int fchmod_of_if(ffs *fs, int *of, unsigned long mode, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = chmod_ok(fs, fs->ofs[*of].ino, mode);
    return r;
}

int ffs_fchmod(ffs *fs, int fd, unsigned long mode)
{
    int of;
    int r;

    r = fd_of2(fs, fd, &of);
    r = fail_code(r);
    r = fchmod_of_if(fs, &of, mode, r);
    return r;
}

static int fchown_of_if(ffs *fs, int *of, unsigned long uid, unsigned long gid,
                        int r)
{
    if (r < 0)
    {
        return r;
    }
    r = chown_ok(fs, fs->ofs[*of].ino, uid, gid);
    return r;
}

int ffs_fchown(ffs *fs, int fd, unsigned long uid, unsigned long gid)
{
    int of;
    int r;

    r = fd_of2(fs, fd, &of);
    r = fail_code(r);
    r = fchown_of_if(fs, &of, uid, gid, r);
    return r;
}

static int fchdir_set(ffs *fs, int of)
{
    fs->cwd = fs->ofs[of].ino;
    return 0;
}

static int fchdir_dir_if(ffs *fs, int *of, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = choose(fs->ofs[*of].is_dir == 0, -ENOTDIR, 0);
    return r;
}

static int fchdir_search_if(ffs *fs, int *of, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = check_search(fs, fs->ofs[*of].ino);
    return r;
}

static int fchdir_prep(ffs *fs, int fd, int *of)
{
    int r;

    r = fd_of(fs, fd, of);
    r = fchdir_dir_if(fs, of, r);
    r = fchdir_search_if(fs, of, r);
    return r;
}

static int fchdir_set_if(ffs *fs, int *of, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = fchdir_set(fs, *of);
    return r;
}

int ffs_fchdir(ffs *fs, int fd)
{
    int of;
    int r;

    r = fchdir_prep(fs, fd, &of);
    r = fail_code(r);
    r = fchdir_set_if(fs, &of, r);
    return r;
}

static int dup_if(ffs *fs, int fd, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = dup_min(fs, fd, 0);
    return r;
}

static int dup_run(ffs *fs, int fd)
{
    int r;

    r = choose(fs == NULL, -EINVAL, 0);
    r = dup_if(fs, fd, r);
    r = fail_code(r);
    return r;
}

int ffs_dup(ffs *fs, int fd)
{
    int r;

    r = dup_run(fs, fd);
    return r;
}

int fd_pair_bad(const ffs *fs, int oldfd, int newfd)
{
    if (fs == NULL)
    {
        return 1; /* LCOV_EXCL_LINE */
    }
    return (fd_valid(fs, oldfd) == 0) + (fd_range_ok(newfd) == 0);
}

static int dup2_check(ffs *fs, int oldfd, int newfd)
{
    int bad;

    bad = fd_pair_bad(fs, oldfd, newfd);
    return choose(bad, -EBADF, 0);
}

static void dup2_close(ffs *fs, int newfd)
{
    int o;

    o = fs->fds[newfd].of;
    fs->fds[newfd].used = 0;
    of_release(fs, o);
}

static void dup_set(ffs *fs, int newfd, int of)
{
    fs->fds[newfd].used = 1;
    fs->fds[newfd].of = of;
    ++fs->ofs[of].refs;
}

static int dup_into(ffs *fs, int oldfd, int newfd)
{
    int of;

    of = fs->fds[oldfd].of;
    dup_set(fs, newfd, of);
    return newfd;
}

static void dup2_close_if(ffs *fs, int newfd)
{
    if (fs->fds[newfd].used)
    {
        dup2_close(fs, newfd);
    }
}

static void dup2_move(ffs *fs, int oldfd, int newfd, int *r)
{
    dup2_close_if(fs, newfd);
    *r = dup_into(fs, oldfd, newfd);
}

static int dup2_do(ffs *fs, int oldfd, int newfd)
{
    int r;

    r = newfd;
    if (newfd != oldfd)
    {
        dup2_move(fs, oldfd, newfd, &r);
    }
    return r;
}

static int dup2_if(ffs *fs, int oldfd, int newfd, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = dup2_do(fs, oldfd, newfd);
    return r;
}

int ffs_dup2(ffs *fs, int oldfd, int newfd)
{
    int r;

    r = dup2_check(fs, oldfd, newfd);
    r = fail_code(r);
    r = dup2_if(fs, oldfd, newfd, r);
    return r;
}

static int pread_check(ffs *fs, int fd, struct rw_ctx *c)
{
    int r;

    r = choose(either_null(fs, c->buf), -EINVAL, 0);
    r = choose(c->off < 0, -EINVAL, r);
    r = read_check_if(fs, fd, c, r);
    return r;
}

static int pread_load(ffs *fs, struct rw_ctx *c, struct finode *fi)
{
    int r;

    r = inode_load(fs, fs->ofs[c->of].ino, fi);
    c->pos = (ffs_off)c->off;
    return choose(r < 0, r, 0);
}

static int pread_load_if(ffs *fs, struct rw_ctx *c, struct finode *fi, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = pread_load(fs, c, fi);
    return r;
}

static int pread_prep(ffs *fs, int fd, struct rw_ctx *c, struct finode *fi)
{
    int r;

    r = pread_check(fs, fd, c);
    r = pread_load_if(fs, c, fi, r);
    return r;
}

static void pread_commit(ffs *fs, struct rw_ctx *c, struct finode *fi)
{
    read_data(fs, c);
    if (fs->atime_on)
    {
        touch_atime(fs, fi);
    }
}

static ssize_t pread_commit_if(ffs *fs, struct rw_ctx *c, struct finode *fi)
{
    if (c->want == 0)
    {
        return 0;
    }
    pread_commit(fs, c, fi);
    return (ssize_t)c->want;
}

static ssize_t pread_done(ffs *fs, struct rw_ctx *c, struct finode *fi)
{
    ssize_t out;

    c->want = read_want(fi, c->pos, c->n);
    out = pread_commit_if(fs, c, fi);
    inode_free(fi);
    return out;
}

static ssize_t pread_prep_ok(ffs *fs, int fd, struct rw_ctx *c,
                             struct finode *fi)
{
    int r;

    r = pread_prep(fs, fd, c, fi);
    r = fail_code(r);
    return (ssize_t)r;
}

static ssize_t pread_finish_if(ffs *fs, struct rw_ctx *c, struct finode *fi)
{
    if (guard_skip((int)c->r, 0))
    {
        return c->r;
    }
    c->r = pread_done(fs, c, fi);
    return c->r;
}

static ssize_t pread_body(ffs *fs, int fd, struct rw_ctx *c)
{
    struct finode fi;

    c->r = pread_prep_ok(fs, fd, c, &fi);
    c->r = pread_finish_if(fs, c, &fi);
    return c->r;
}

ssize_t ffs_pread(ffs *fs, int fd, void *buf, size_t n, off_t off)
{
    struct rw_ctx c;
    ssize_t out;

    rw_set(&c, buf, n, off);
    out = pread_body(fs, fd, &c);
    return out;
}

static int pwrite_check(ffs *fs, int fd, const void *buf, struct wctx *c,
                        off_t off)
{
    int r;

    r = choose(either_null(fs, buf), -EINVAL, 0);
    r = choose(off < 0, -EINVAL, r);
    r = write_check_if(fs, fd, c, r);
    return r;
}

static ssize_t pwrite_prep_ok(struct wctx *c)
{
    int r;

    r = write_end(c, c->n);
    r = fail_code(r);
    return (ssize_t)r;
}

static ssize_t pwrite_n_if(int r, size_t n)
{
    if (r < 0)
    {
        return (ssize_t)r;
    }
    return (ssize_t)n;
}

static ssize_t pwrite_txn_ok(ffs *fs, struct wctx *c, ssize_t out)
{
    if (guard_skip((int)out, 0))
    {
        return out;
    }
    out = (ssize_t)write_txn_run(fs, c);
    return pwrite_n_if((int)out, c->n);
}

static ssize_t pwrite_body(ffs *fs, struct wctx *c)
{
    ssize_t out;

    out = pwrite_prep_ok(c);
    out = pwrite_txn_ok(fs, c, out);
    return out;
}

static ssize_t pwrite_body_if(ffs *fs, struct wctx *c, ssize_t out)
{
    if (guard_skip((int)out, c->n == 0))
    {
        return out;
    }
    out = pwrite_body(fs, c);
    return out;
}

static void pwrite_set(struct wctx *c, const void *buf, size_t n, off_t off)
{
    c->buf = buf;
    c->n = n;
    c->pos = (ffs_off)off;
}

static ssize_t pwrite_checked(ffs *fs, struct wctx *c, int r, const void *buf,
                              size_t n, off_t off)
{
    ssize_t out;

    pwrite_set(c, buf, n, off);
    out = (ssize_t)fail_code(r);
    out = pwrite_body_if(fs, c, out);
    return out;
}

ssize_t ffs_pwrite(ffs *fs, int fd, const void *buf, size_t n, off_t off)
{
    struct wctx c;
    int r;
    ssize_t out;

    r = pwrite_check(fs, fd, buf, &c, off);
    out = pwrite_checked(fs, &c, r, buf, n, off);
    return out;
}

static int fcntl_getfl(ffs *fs, int of)
{
    return fs->ofs[of].flags & (O_ACCMODE | O_APPEND);
}

static int fcntl_setfl(ffs *fs, int of, long arg)
{
    fs->ofs[of].flags = (fs->ofs[of].flags & ~O_APPEND) | ((int)arg & O_APPEND);
    return 0;
}

static int ref_after(ffs *fs, int of, int nfd)
{
    if (nfd < 0)
    {
        return nfd;
    }
    ++fs->ofs[of].refs;
    return nfd;
}

static int fcntl_dupfd(ffs *fs, int of, long arg)
{
    int nfd;

    nfd = fd_alloc2(fs, of, (int)arg);
    nfd = ref_after(fs, of, nfd);
    return nfd;
}

static int fcntl_kind(int cmd)
{
    int k;

    k = choose(cmd == F_SETFL, 2, 0);
    k = choose(cmd == F_DUPFD, 3, k);
    k = choose(cmd == F_GETFL, 1, k);
    return k;
}

static int fcntl_get_if(ffs *fs, int of, int k, int r)
{
    if (k != 1)
    {
        return r;
    }
    r = fcntl_getfl(fs, of);
    return r;
}

static int fcntl_set_if(ffs *fs, int of, int k, long arg, int r)
{
    if (k != 2)
    {
        return r;
    }
    r = fcntl_setfl(fs, of, arg);
    return r;
}

static int fcntl_dup_if(ffs *fs, int of, int k, long arg, int r)
{
    if (k != 3)
    {
        return r;
    }
    r = fcntl_dupfd(fs, of, arg);
    return r;
}

static int fcntl_go(ffs *fs, int of, int k, long arg, int r)
{
    r = fcntl_get_if(fs, of, k, r);
    r = fcntl_set_if(fs, of, k, arg, r);
    r = fcntl_dup_if(fs, of, k, arg, r);
    return r;
}

static int fcntl_cmd(ffs *fs, int of, int cmd, long arg)
{
    int k;
    int r;

    k = fcntl_kind(cmd);
    r = choose(k == 0, -EINVAL, 0);
    r = fcntl_go(fs, of, k, arg, r);
    return r;
}

static int fcntl_if(ffs *fs, int *of, int cmd, long arg, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = fcntl_cmd(fs, *of, cmd, arg);
    r = fail_code(r);
    return r;
}

int ffs_fcntl(ffs *fs, int fd, int cmd, long arg)
{
    int of;
    int r;

    r = fd_of(fs, fd, &of);
    r = fail_code(r);
    r = fcntl_if(fs, &of, cmd, arg, r);
    return r;
}
