#include "fakefs_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct resolve_ctx
{
    ffs *fs;
    int hops;
    int absolute;
    int trailing;
    long cur;
    int ncomps;
    long *po;
    char *no;
    long *io;
    char buf[FFS_PATH_MAX + 1];
    char *comps[FFS_MAX_COMPS];
};

int symlink_mode(long mode)
{
    unsigned long bits;

    bits = (unsigned long)mode;
    return (bits & FFS_S_IFMT) == FFS_S_IFLNK;
}

static long symlink_ino_if(long ino, long mode, int r)
{
    if (r < 0)
    {
        return -ENOENT; /* LCOV_EXCL_LINE */
    }
    return choose_long(symlink_mode(mode), ino, 0);
}

static long symlink_ino(ffs *fs, long ino)
{
    long mode;
    int r;

    mode = 0;
    r = inode_mode(fs, ino, &mode);
    r = (int)symlink_ino_if(ino, mode, r);
    return r;
}

int comp_is_dot(const char *c)
{
    if (strlen(c) != 1)
    {
        return 0;
    }
    return c[0] == '.';
}

int comp_is_dotdot(const char *c)
{
    if (strlen(c) != 2)
    {
        return 0;
    }
    return (c[0] == '.') & (c[1] == '.');
}

static struct rres rres_new(long *parent, char *name, long *ino)
{
    struct rres r;

    r.parent = parent;
    r.name = name;
    r.ino = ino;
    return r;
}

static void ctx_begin(struct resolve_ctx *c, ffs *fs, int hops, struct rres r)
{
    c->fs = fs;
    c->hops = hops;
    c->po = r.parent;
    c->no = r.name;
    c->io = r.ino;
}

int follow_of(int follow_last, int follow_trailing, int trailing)
{
    return follow_last | (trailing & follow_trailing);
}

static void set_long(long *p, long v)
{
    *p = v;
}

static void set_long_if(long *p, long v)
{
    if (p == NULL)
    {
        return;
    }
    set_long(p, v);
}

static void out_self(struct resolve_ctx *c, long cur)
{
    set_long_if(c->po, cur);
    set_long_if(c->io, cur);
    if (c->no)
    {
        c->no[0] = '\0';
    }
}

static int self_at(struct resolve_ctx *c, long cur)
{
    out_self(c, cur);
    return 0;
}

static int empty_result(struct resolve_ctx *c)
{
    int r;

    r = self_at(c, c->cur);
    return r;
}

static void out_name(struct resolve_ctx *c, const char *name, long parent)
{
    set_long_if(c->po, parent);
    if (c->no)
    {
        memcpy(c->no, name, strlen(name) + 1);
    }
}

static void copy_name(char *no, const char *name, int follow)
{
    if (follow)
    {
        no[0] = '\0';
        return;
    }
    memcpy(no, name, strlen(name) + 1);
}

static void name_or_empty(char *no, const char *name, int follow)
{
    if (no == NULL)
    {
        return;
    }
    copy_name(no, name, follow);
}

static void last_prep(struct resolve_ctx *c, long cur, const char *comp,
                      int follow)
{
    set_long_if(c->po, cur);
    name_or_empty(c->no, comp, follow);
}

static long trail_ino(ffs *fs, long ino, int trailing)
{
    int isd;

    isd = is_dir(fs, ino);
    return choose_long(trailing & (isd == 0), -ENOTDIR, ino);
}

static void out_name_if(struct resolve_ctx *c, long e, const char *comp,
                        long cur)
{
    if (e < 0)
    {
        out_name(c, comp, cur);
    }
}

static long last_lookup(ffs *fs, long cur, const char *comp,
                        struct resolve_ctx *c)
{
    long e;

    e = dirent_lookup(fs, cur, comp);
    out_name_if(c, e, comp, cur);
    return e;
}

static long trail_if(ffs *fs, long e, int trailing, long r)
{
    if (r < 0)
    {
        return r;
    }
    r = trail_ino(fs, e, trailing);
    return r;
}

static long last_follow(ffs *fs, long e, long cur, int follow, int hops,
                        int trailing, struct resolve_ctx *c)
{
    long r;

    e = maybe_follow(fs, e, cur, follow, hops, c->po, c->no);
    r = trail_if(fs, e, trailing, e);
    return r;
}

static int commit_ino(struct resolve_ctx *c, long e)
{
    if (c->io)
    {
        *c->io = e;
    }
    return 0;
}

static int commit_ino_if(struct resolve_ctx *c, long e, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = commit_ino(c, e);
    return r;
}

static int commit_if(struct resolve_ctx *c, long e)
{
    int r;

    r = (int)e;
    r = commit_ino_if(c, e, r);
    return r;
}

static int last_commit(ffs *fs, long e, long cur, int follow, int hops,
                       int trailing, struct resolve_ctx *c)
{
    int r;

    e = last_follow(fs, e, cur, follow, hops, trailing, c);
    r = commit_if(c, e);
    return r;
}

static int find_prep_if(struct resolve_ctx *c, long cur, const char *comp,
                        int follow, int r)
{
    if (r < 0)
    {
        return r;
    }
    last_prep(c, cur, comp, follow);
    return 0;
}

static int find_prep(struct resolve_ctx *c, long e, long cur, const char *comp,
                     int follow)
{
    int r;

    r = neg_code(e);
    r = find_prep_if(c, cur, comp, follow, r);
    return r;
}

static int find_commit(ffs *fs, long e, long cur, int follow, int hops,
                       int trailing, struct resolve_ctx *c, int r)
{
    int d;

    if (r < 0)
    {
        return r;
    }
    d = last_commit(fs, e, cur, follow, hops, trailing, c);
    return d;
}

static int find_done(ffs *fs, long e, long cur, const char *comp, int hops,
                     int follow, int trailing, struct resolve_ctx *c)
{
    int r;

    r = find_prep(c, e, cur, comp, follow);
    r = find_commit(fs, e, cur, follow, hops, trailing, c, r);
    return r;
}

static int last_find(ffs *fs, long cur, const char *comp, int hops, int follow,
                     int trailing, struct resolve_ctx *c)
{
    long e;
    int r;

    e = last_lookup(fs, cur, comp, c);
    r = find_done(fs, e, cur, comp, hops, follow, trailing, c);
    return r;
}

static int self_at_if(struct resolve_ctx *c, long p, int r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = self_at(c, p);
    return r;
}

static int dotdot_at(struct resolve_ctx *c, long p)
{
    int r;

    r = neg_code(p);
    r = self_at_if(c, p, r);
    return r;
}

static int last_dotdot(ffs *fs, long cur, struct resolve_ctx *c)
{
    long p;
    int r;

    p = parent_of(fs, cur);
    r = dotdot_at(c, p);
    return r;
}

static int last_after(ffs *fs, long cur, const char *comp, int hops, int follow,
                      int trailing, struct resolve_ctx *c)
{
    int r;

    if (comp_is_dot(comp))
    {
        r = self_at(c, cur);
        return r;
    }
    if (comp_is_dotdot(comp))
    {
        r = last_dotdot(fs, cur, c);
        return r;
    }
    r = last_find(fs, cur, comp, hops, follow, trailing, c);
    return r;
}

static int step_last_if(ffs *fs, long cur, const char *comp, int hops,
                        int follow, int trailing, struct resolve_ctx *c, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = last_after(fs, cur, comp, hops, follow, trailing, c);
    return r;
}

static int step_last(ffs *fs, long cur, const char *comp, int hops, int follow,
                     int trailing, struct resolve_ctx *c)
{
    int r;

    r = check_search(fs, cur);
    r = step_last_if(fs, cur, comp, hops, follow, trailing, c, r);
    return r;
}

static long require_dir(ffs *fs, long ino)
{
    int isd;

    isd = is_dir(fs, ino);
    return choose_long(isd == 0, -ENOTDIR, ino);
}

static long require_dir_if(ffs *fs, long e, long r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = require_dir(fs, e);
    return r;
}

static long require_if(ffs *fs, long e)
{
    long r;

    r = e;
    r = require_dir_if(fs, e, r);
    return r;
}

static long mid_follow(ffs *fs, long e, long cur, int hops)
{
    long r;

    e = maybe_follow(fs, e, cur, 1, hops, NULL, NULL);
    r = require_if(fs, e);
    return r;
}

static long mid_follow_if(ffs *fs, long e, long cur, int hops, long r)
{
    if (r < 0)
    {
        return r;
    }
    r = mid_follow(fs, e, cur, hops);
    return r;
}

static long mid_if(ffs *fs, long e, long cur, int hops)
{
    long r;

    r = e;
    r = mid_follow_if(fs, e, cur, hops, r);
    return r;
}

static long step_mid(ffs *fs, long cur, const char *comp, int hops)
{
    long e;
    long r;

    e = dirent_lookup(fs, cur, comp);
    r = mid_if(fs, e, cur, hops);
    return r;
}

static long mid_parent_if(ffs *fs, long cur, const char *comp)
{
    long r;

    if (!comp_is_dotdot(comp))
    {
        return 0;
    }
    r = parent_of(fs, cur);
    return r;
}

static long mid_step_if(ffs *fs, long cur, const char *comp, int hops, long r)
{
    if (r != 0)
    {
        return r;
    }
    r = step_mid(fs, cur, comp, hops);
    return r;
}

static long mid_dotdot_if(ffs *fs, long cur, const char *comp, int hops)
{
    long r;

    r = mid_parent_if(fs, cur, comp);
    r = mid_step_if(fs, cur, comp, hops, r);
    return r;
}

static long mid_after(ffs *fs, long cur, const char *comp, int hops)
{
    long r;

    if (comp_is_dot(comp))
    {
        return cur;
    }
    r = mid_dotdot_if(fs, cur, comp, hops);
    return r;
}

static long mid_after_if(ffs *fs, long cur, const char *comp, int hops, long r)
{
    if (r < 0)
    {
        return r; /* LCOV_EXCL_LINE */
    }
    r = mid_after(fs, cur, comp, hops);
    return r;
}

static long mid_if_ok(ffs *fs, long cur, const char *comp, int hops, int rc)
{
    long r;

    r = (long)rc;
    r = mid_after_if(fs, cur, comp, hops, r);
    return r;
}

static long mid_hop(ffs *fs, long cur, const char *comp, int hops)
{
    int rc;
    long r;

    rc = check_search(fs, cur);
    r = mid_if_ok(fs, cur, comp, hops, rc);
    return r;
}

static long walk_mid(ffs *fs, long cur, char **comps, int n, int hops)
{
    int i;

    for (i = 0; (i + 1 < n) & (cur >= 0); ++i)
    {
        cur = mid_hop(fs, cur, comps[i], hops);
    }
    return cur;
}

static int last_step_if(ffs *fs, long ncur, char **comps, int n, int hops,
                        int follow, int trailing, struct resolve_ctx *c, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = step_last(fs, ncur, comps[n - 1], hops, follow, trailing, c);
    return r;
}

static int last_if(ffs *fs, long ncur, char **comps, int n, int hops,
                   int follow, int trailing, struct resolve_ctx *c)
{
    int r;

    r = neg_code(ncur);
    r = last_step_if(fs, ncur, comps, n, hops, follow, trailing, c, r);
    return r;
}

static int walk_last(ffs *fs, long cur, char **comps, int n, int hops,
                     int follow, int trailing, struct resolve_ctx *c)
{
    long ncur;
    int r;

    ncur = walk_mid(fs, cur, comps, n, hops);
    r = last_if(fs, ncur, comps, n, hops, follow, trailing, c);
    return r;
}

static void split_root(struct resolve_ctx *c)
{
    c->cur = c->fs->root;
}

static void split_start(struct resolve_ctx *c, long start)
{
    if (c->absolute)
    {
        split_root(c);
        return;
    }
    c->cur = start;
}

static int split_seen(struct resolve_ctx *c, int n, long start)
{
    c->ncomps = n;
    if (n < 0)
    {
        return n;
    }
    split_start(c, start);
    return 0;
}

static int split_prep(struct resolve_ctx *c, const char *path, long start)
{
    int n;
    int r;

    n = path_split(path, c->buf, c->comps, FFS_MAX_COMPS, &c->absolute,
                   &c->trailing);
    r = split_seen(c, n, start);
    return r;
}

static int walk_run(struct resolve_ctx *c, int follow)
{
    int r;

    r = walk_last(c->fs, c->cur, c->comps, c->ncomps, c->hops, follow,
                  c->trailing, c);
    return r;
}

static int walk_choose(struct resolve_ctx *c, int follow)
{
    int r;

    if (c->ncomps == 0)
    {
        r = empty_result(c);
        return r;
    }
    r = walk_run(c, follow);
    return r;
}

static int walk_from(struct resolve_ctx *c, int follow_last,
                     int follow_trailing)
{
    int follow;
    int r;

    follow = follow_of(follow_last, follow_trailing, c->trailing);
    r = walk_choose(c, follow);
    return r;
}

static void boot_init(struct resolve_ctx *c, ffs *fs)
{
    memset(c, 0, sizeof(*c));
    c->fs = fs;
}

static int resolve_init(struct resolve_ctx *c, ffs *fs, const char *path,
                        long start, int hops, struct rres r)
{
    int r2;

    boot_init(c, fs);
    ctx_begin(c, fs, hops, r);
    r2 = split_prep(c, path, start);
    return r2;
}

static int boot_walk_if(struct resolve_ctx *c, int follow_last,
                        int follow_trailing, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = walk_from(c, follow_last, follow_trailing);
    return r;
}

static int boot_walk(struct resolve_ctx *c, int r2, int follow_last,
                     int follow_trailing)
{
    int r;

    r = choose(r2 < 0, c->ncomps, 0);
    r = boot_walk_if(c, follow_last, follow_trailing, r);
    return r;
}

static int resolve_boot(ffs *fs, const char *path, long start, int hops,
                        int follow_last, int follow_trailing, struct rres r)
{
    struct resolve_ctx c;
    int r2;

    r2 = resolve_init(&c, fs, path, start, hops, r);
    r2 = boot_walk(&c, r2, follow_last, follow_trailing);
    return r2;
}

int resolve_from(ffs *fs, const char *path, long start, int hops,
                 int follow_last, int follow_trailing, struct rres r)
{
    int r2;

    r2 = resolve_boot(fs, path, start, hops, follow_last, follow_trailing, r);
    return r2;
}

static long finish_code(char *target, int r, long tino)
{
    free(target);
    if (r < 0)
    {
        return (long)r;
    }
    return tino;
}

static long target_finish(ffs *fs, char *target, long dir, int hops, long *po,
                          char *no, long *tino)
{
    long out;
    int r;

    r = resolve_from(fs, target, dir, hops - 1, 1, 1, rres_new(po, no, tino));
    out = finish_code(target, r, *tino);
    return out;
}

static long target_resolve(ffs *fs, char *target, long dir, int hops, long *po,
                           char *no)
{
    long tino;
    long out;

    tino = 0;
    out = target_finish(fs, target, dir, hops, po, no, &tino);
    return out;
}

static long hop_done(char *target)
{
    free(target);
    return -ELOOP;
}

static long follow_zero_if(char *target, int hops)
{
    long r;

    if (hops > 0)
    {
        return 0;
    }
    r = hop_done(target);
    return r;
}

static long follow_resolve_if(ffs *fs, char *target, long dir, int hops,
                              long *po, char *no, long r)
{
    if (r != 0)
    {
        return r;
    }
    r = target_resolve(fs, target, dir, hops, po, no);
    return r;
}

static long follow_hops(ffs *fs, char *target, long dir, int hops, long *po,
                        char *no)
{
    long r;

    r = follow_zero_if(target, hops);
    r = follow_resolve_if(fs, target, dir, hops, po, no, r);
    return r;
}

static long follow_hops_if(ffs *fs, char *target, long dir, int hops, long *po,
                           char *no, long r)
{
    if (r < 0)
    {
        return -ENOENT; /* LCOV_EXCL_LINE */
    }
    r = follow_hops(fs, target, dir, hops, po, no);
    return r;
}

static long follow_step(ffs *fs, long ino, long dir, int hops, long *po,
                        char *no)
{
    char *target;
    int r;
    long out;

    r = inode_target(fs, ino, &target);
    out = follow_hops_if(fs, target, dir, hops, po, no, r);
    return out;
}

static long follow_into(ffs *fs, long ino, long dir, int hops, long *po,
                        char *no)
{
    long r;

    r = follow_step(fs, ino, dir, hops, po, no);
    return r;
}

static long maybe_target(ffs *fs, long ino, long dir, int hops, long *po,
                         char *no)
{
    long t;
    long r;

    t = symlink_ino(fs, ino);
    if (t < 0)
    {
        return t;
    }
    if (t > 0)
    {
        r = follow_into(fs, ino, dir, hops, po, no);
        return r;
    }
    return ino;
}

static long follow_target_if(ffs *fs, long ino, long dir, int hops, long *po,
                             char *no, int do_follow, long r)
{
    if (do_follow == 0)
    {
        return r;
    }
    r = maybe_target(fs, ino, dir, hops, po, no);
    return r;
}

static long follow_if(ffs *fs, long ino, long dir, int hops, long *po, char *no,
                      int do_follow)
{
    long r;

    r = ino;
    r = follow_target_if(fs, ino, dir, hops, po, no, do_follow, r);
    return r;
}

long maybe_follow(ffs *fs, long ino, long dir, int do_follow, int hops,
                  long *parent_out, char *name_out)
{
    long r;

    r = follow_if(fs, ino, dir, hops, parent_out, name_out, do_follow);
    return r;
}

int resolve(ffs *fs, const char *path, int follow_last, long *parent,
            char *name, long *ino)
{
    int r;

    r = resolve_at(fs, AT_FDCWD, path, follow_last, 1, parent, name, ino);
    return r;
}

static int atcwd(int dirfd)
{
    return dirfd == AT_FDCWD;
}

static int cwd_ino(ffs *fs, long *out)
{
    *out = fs->cwd;
    return 0;
}

static int of_dir_ino(ffs *fs, int of, long *out)
{
    if (fs->ofs[of].is_dir == 0)
    {
        return -ENOTDIR;
    }
    *out = fs->ofs[of].ino;
    return 0;
}

static int dir_ino_if(ffs *fs, int *of, long *out, int r)
{
    if (r < 0)
    {
        return r;
    }
    r = of_dir_ino(fs, *of, out);
    return r;
}

static int dir_ino(ffs *fs, int dirfd, long *out)
{
    int of;
    int r;

    r = fd_of(fs, dirfd, &of);
    r = dir_ino_if(fs, &of, out, r);
    return r;
}

static int cwd_if(ffs *fs, long *out, int at)
{
    int r;

    if (!at)
    {
        return 0;
    }
    r = cwd_ino(fs, out);
    return r;
}

static int dir_if(ffs *fs, int dirfd, long *out, int at, int r)
{
    if (at)
    {
        return r;
    }
    r = dir_ino(fs, dirfd, out);
    return r;
}

int fd_to_inode(ffs *fs, int dirfd, long *out)
{
    int at;
    int r;

    at = atcwd(dirfd);
    r = cwd_if(fs, out, at);
    r = dir_if(fs, dirfd, out, at, r);
    return r;
}

static long fd_start(ffs *fs, int dirfd)
{
    long ino;
    int r;

    ino = 0;
    r = fd_to_inode(fs, dirfd, &ino);
    return choose_long(r < 0, (long)r, ino);
}

static int resolve_after_if(ffs *fs, long start, const char *path,
                            int follow_last, int follow_trailing, struct rres r,
                            int r2)
{
    if (r2 < 0)
    {
        return r2;
    }
    r2 = resolve_from(fs, path, start, FFS_MAX_SYMLINK, follow_last,
                      follow_trailing, r);
    return r2;
}

static int resolve_after(ffs *fs, long start, const char *path, int follow_last,
                         int follow_trailing, struct rres r)
{
    int r2;

    r2 = neg_code(start);
    r2 = resolve_after_if(fs, start, path, follow_last, follow_trailing, r, r2);
    return r2;
}

static int resolve_at_run(ffs *fs, int dirfd, const char *path, int follow_last,
                          int follow_trailing, struct rres r)
{
    long start;
    int r2;

    start = fd_start(fs, dirfd);
    r2 = resolve_after(fs, start, path, follow_last, follow_trailing, r);
    return r2;
}

int resolve_at(ffs *fs, int dirfd, const char *path, int follow_last,
               int follow_trailing, long *parent, char *name, long *ino)
{
    int r;

    r = resolve_at_run(fs, dirfd, path, follow_last, follow_trailing,
                       rres_new(parent, name, ino));
    return r;
}

static int anc_found(long a, long cur)
{
    if (cur == a)
    {
        return 1;
    }
    return 0;
}

static int anc_step(ffs *fs, long a, long *cur, long *guard)
{
    long p;

    if (*cur == a)
    {
        return 1;
    }
    ++(*guard);
    if (*guard >= FFS_PATH_MAX)
    {
        return -1;
    }
    p = parent_of(fs, *cur);
    *cur = p;
    return 0;
}

int is_ancestor(ffs *fs, long a, long b)
{
    long cur;
    long guard;
    int r;
    int found;

    cur = b;
    guard = 0;
    r = 0;
    while (r == 0)
    {
        r = anc_step(fs, a, &cur, &guard);
    }
    found = anc_found(a, cur);
    return found;
}
