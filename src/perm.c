#include "fakefs_internal.h"

#include <errno.h>

int fs_is_root(const ffs *fs)
{
    if (fs->uid == 0)
    {
        return 1;
    }
    return 0;
}

int root_ok(long mode, int need)
{
    if (need != FFS_PERM_EXEC)
    {
        return 1;
    }
    return (mode & FFS_EXEC_ANY) != 0;
}

int group_shift(const ffs *fs, long gid)
{
    if (fs->gid == gid)
    {
        return FFS_SHIFT_GROUP;
    }
    return FFS_SHIFT_OTHER;
}

int shift_for(const ffs *fs, long uid, long gid)
{
    if (fs->uid == uid)
    {
        return FFS_SHIFT_OWNER;
    }
    return group_shift(fs, gid);
}

int perm_ok(const ffs *fs, long mode, long uid, long gid, int need)
{
    if (fs_is_root(fs))
    {
        return root_ok(mode, need);
    }
    return ((mode >> shift_for(fs, uid, gid)) & FFS_PERM_BITS & need) != 0;
}

int meta_code(int r)
{
    if (r < 0)
    {
        return -EACCES; /* LCOV_EXCL_LINE */
    }
    return 0;
}

static int meta_perm(ffs *fs, long ino, struct perm_meta *m)
{
    int r;

    r = inode_meta(fs, ino, &m->mode, &m->uid, &m->gid);
    return meta_code(r);
}

static int perm_after(ffs *fs, const struct perm_meta *m, int need, int r)
{
    if (r < 0)
    {
        return r;
    }
    return perm_ok(fs, m->mode, m->uid, m->gid, need);
}

static int perm_ok_ino(ffs *fs, long ino, int need)
{
    struct perm_meta m;
    int r;

    r = meta_perm(fs, ino, &m);
    r = perm_after(fs, &m, need, r);
    return r;
}

int access_err(int ok)
{
    if (ok > 0)
    {
        return 0;
    }
    return -EACCES;
}

int perm_denied(int ok)
{
    if (ok)
    {
        return 0;
    }
    return -EPERM;
}

int owner_match(const ffs *fs, const struct perm_meta *m)
{
    return fs->uid == m->uid;
}

static int owner_after(ffs *fs, const struct perm_meta *m, int r)
{
    if (r < 0)
    {
        return r;
    }
    return perm_denied(owner_match(fs, m));
}

static int owner_ok(ffs *fs, long ino)
{
    struct perm_meta m;
    int r;

    r = meta_perm(fs, ino, &m);
    r = owner_after(fs, &m, r);
    return r;
}

static int times_owner(ffs *fs, const struct perm_meta *m)
{
    if (fs->uid == m->uid)
    {
        return 0;
    }
    return perm_denied(perm_ok(fs, m->mode, m->uid, m->gid, FFS_PERM_WRITE));
}

static int times_after(ffs *fs, const struct perm_meta *m, int r)
{
    if (r < 0)
    {
        return r;
    }
    return times_owner(fs, m);
}

static int times_ok(ffs *fs, long ino)
{
    struct perm_meta m;
    int r;

    r = meta_perm(fs, ino, &m);
    r = times_after(fs, &m, r);
    return r;
}

int choose(int on, int on_true, int on_false)
{
    if (on)
    {
        return on_true;
    }
    return on_false;
}

int chain(int r, int next)
{
    if (r < 0)
    {
        return r;
    }
    return next;
}

static int check_root_then(ffs *fs, long ino, perm_fn chk)
{
    int r;

    r = chk(fs, ino);
    return choose(fs_is_root(fs) == 0, r, 0);
}

int check_search(ffs *fs, long ino)
{
    int ok;

    ok = perm_ok_ino(fs, ino, FFS_PERM_EXEC);
    return access_err(ok);
}

int check_read_file(ffs *fs, long ino)
{
    int ok;

    ok = perm_ok_ino(fs, ino, FFS_PERM_READ);
    return access_err(ok);
}

int check_write_file(ffs *fs, long ino)
{
    int ok;

    ok = perm_ok_ino(fs, ino, FFS_PERM_WRITE);
    return access_err(ok);
}

int check_list_dir(ffs *fs, long ino)
{
    int ok;

    ok = perm_ok_ino(fs, ino, FFS_PERM_READ);
    return access_err(ok);
}

static int create_bits(ffs *fs, long ino, int ok)
{
    int r;

    r = perm_ok_ino(fs, ino, FFS_PERM_EXEC);
    return choose(ok > 0, r, ok);
}

int check_create_dir(ffs *fs, long ino)
{
    int ok;

    ok = perm_ok_ino(fs, ino, FFS_PERM_WRITE);
    ok = create_bits(fs, ino, ok);
    return access_err(ok);
}

int check_owner(ffs *fs, long ino)
{
    int r;

    r = check_root_then(fs, ino, owner_ok);
    return r;
}

int check_times(ffs *fs, long ino)
{
    int r;

    r = check_root_then(fs, ino, times_ok);
    return r;
}

int need_read(int acc)
{
    if (acc == O_RDONLY)
    {
        return 1;
    }
    return acc == O_RDWR;
}

int need_write(int acc)
{
    if (acc == O_WRONLY)
    {
        return 1;
    }
    return acc == O_RDWR;
}

static int req_read(ffs *fs, long ino, int acc)
{
    int r;

    r = check_read_file(fs, ino);
    return choose(need_read(acc), r, 0);
}

static int req_write(ffs *fs, long ino, int acc)
{
    int r;

    r = check_write_file(fs, ino);
    return choose(need_write(acc), r, 0);
}

static int check_rw(ffs *fs, long ino, int flags)
{
    int r;
    int w;

    r = req_read(fs, ino, flags & O_ACCMODE);
    w = req_write(fs, ino, flags & O_ACCMODE);
    return chain(r, w);
}

static int req_bit(ffs *fs, long ino, int amode, int bit, perm_fn chk)
{
    int r;

    r = chk(fs, ino);
    return choose(amode & bit, r, 0);
}

static int trunc_check(ffs *fs, long ino, int flags)
{
    int r;

    r = req_bit(fs, ino, flags, O_TRUNC, check_write_file);
    return r;
}

int check_access(ffs *fs, long ino, int flags)
{
    int r;
    int t;

    r = check_rw(fs, ino, flags);
    t = trunc_check(fs, ino, flags);
    return chain(r, t);
}

static int check_amode(ffs *fs, long ino, int amode)
{
    int rd;
    int wr;

    rd = req_bit(fs, ino, amode, R_OK, check_read_file);
    wr = req_bit(fs, ino, amode, W_OK, check_write_file);
    return chain(rd, wr);
}

static int access_chain(ffs *fs, long ino, int amode)
{
    int rw;
    int x;

    rw = check_amode(fs, ino, amode);
    x = req_bit(fs, ino, amode, X_OK, check_search);
    return chain(rw, x);
}

int access_ok(ffs *fs, long ino, int amode)
{
    int r;

    r = access_chain(fs, ino, amode);
    return r;
}
