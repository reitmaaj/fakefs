#include "fakefs_internal.h"

#include <errno.h>

int of_slot_match(const ffs *fs, int i, long ino)
{
    if (fs->ofs[i].used == 0)
    {
        return 0;
    }
    return fs->ofs[i].ino == ino;
}

static int of_acc(const ffs *fs, int i, long ino, int c)
{
    int m;

    m = of_slot_match(fs, i, ino);
    return c + m;
}

static int of_acc_from(const ffs *fs, long ino, int i, int c)
{
    for (; i < FFS_MAX_FD; ++i)
    {
        c = of_acc(fs, i, ino, c);
    }
    return c;
}

int ino_fd_refs(ffs *fs, long ino)
{
    int c;

    c = of_acc_from(fs, ino, 0, 0);
    return c;
}

int of_taken(const ffs *fs, int i)
{
    if (i >= FFS_MAX_FD)
    {
        return 0;
    }
    return fs->ofs[i].used != 0;
}

int slot_or_err(int i)
{
    if (i >= FFS_MAX_FD)
    {
        return -EMFILE;
    }
    return i;
}

static int of_free_from(const ffs *fs, int i)
{
    for (; of_taken(fs, i); ++i)
    {
    }
    return slot_or_err(i);
}

static int of_init(ffs *fs, int i, long ino, int flags, int is_dir)
{
    fs->ofs[i].used = 1;
    fs->ofs[i].ino = ino;
    fs->ofs[i].offset = 0;
    fs->ofs[i].flags = flags;
    fs->ofs[i].is_dir = is_dir;
    fs->ofs[i].refs = 1;
    return i;
}

int of_alloc(ffs *fs, long ino, int flags, int is_dir)
{
    int i;

    i = of_free_from(fs, 0);
    if (i < 0)
    {
        return i;
    }
    i = of_init(fs, i, ino, flags, is_dir);
    return i;
}

int fd_min(int min)
{
    if (min < 0)
    {
        return 0;
    }
    return min;
}

int fd_taken(const ffs *fs, int i)
{
    if (i >= FFS_MAX_FD)
    {
        return 0;
    }
    return fs->fds[i].used != 0;
}

static int fd_free_from(const ffs *fs, int i)
{
    for (; fd_taken(fs, i); ++i)
    {
    }
    return slot_or_err(i);
}

static int fd_init(ffs *fs, int i, int of)
{
    fs->fds[i].used = 1;
    fs->fds[i].of = of;
    return i;
}

int fd_alloc2(ffs *fs, int of, int min)
{
    int i;

    i = fd_free_from(fs, fd_min(min));
    if (i < 0)
    {
        return i;
    }
    i = fd_init(fs, i, of);
    return i;
}

static int of_ref(ffs *fs, int of, int nfd)
{
    ++fs->ofs[of].refs;
    return nfd;
}

static int dup_alloc(ffs *fs, int of, int min)
{
    int nfd;

    nfd = fd_alloc2(fs, of, min);
    if (nfd < 0)
    {
        return nfd;
    }
    nfd = of_ref(fs, of, nfd);
    return nfd;
}

static int dup_of(ffs *fs, int of, int min)
{
    if (of < 0)
    {
        return of;
    }
    min = dup_alloc(fs, of, min);
    return min;
}

static int dup_source(ffs *fs, int fd)
{
    if (fd_valid(fs, fd) == 0)
    {
        return -EBADF;
    }
    return fs->fds[fd].of;
}

int dup_min(ffs *fs, int fd, int min)
{
    int of;

    of = dup_source(fs, fd);
    min = dup_of(fs, of, min);
    return min;
}
