#include "fakefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static int do_write(const char *path)
{
    ffs *fs;
    int fd;

    fs = ffs_create(path);
    if (!fs)
    {
        return 1;
    }

    if (ffs_mkdir(fs, "/data", 0755) != 0)
    {
        return 1;
    }
    if (ffs_mkdir(fs, "/data/sub", 0755) != 0)
    {
        return 1;
    }

    fd = ffs_open(fs, "/data/sub/file", O_CREAT | O_WRONLY, 0644);
    if (fd < 0)
    {
        return 1;
    }
    if (ffs_write(fs, fd, "persistent-content", 18) != 18)
    {
        return 1;
    }
    if (ffs_close_fd(fs, fd) != 0)
    {
        return 1;
    }

    if (ffs_link(fs, "/data/sub/file", "/data/hard") != 0)
    {
        return 1;
    }
    if (ffs_symlink(fs, "sub/file", "/data/link") != 0)
    {
        return 1;
    }

    ffs_destroy(fs);
    return 0;
}

static int do_verify(const char *path)
{
    ffs *fs;
    struct ffs_stat st;
    struct ffs_stat hl;
    struct ffs_stat tgt;
    char buf[64];
    int fd;

    fs = ffs_create(path);
    if (!fs)
    {
        return 1;
    }

    if (ffs_stat(fs, "/data/sub/file", &st) != 0)
    {
        return 1;
    }
    if (st.st_size != 18)
    {
        return 1;
    }

    fd = ffs_open(fs, "/data/sub/file", O_RDONLY, 0);
    if (fd < 0)
    {
        return 1;
    }
    memset(buf, 0, sizeof(buf));
    if (ffs_read(fs, fd, buf, sizeof(buf)) != 18)
    {
        return 1;
    }
    if (ffs_close_fd(fs, fd) != 0)
    {
        return 1;
    }
    if (strcmp(buf, "persistent-content") != 0)
    {
        return 1;
    }

    if (ffs_stat(fs, "/data/link", &st) != 0)
    {
        return 1;
    }
    if ((st.st_mode & FFS_S_IFMT) != FFS_S_IFREG)
    {
        return 1;
    }

    if (ffs_stat(fs, "/data/hard", &hl) != 0)
    {
        return 1;
    }
    if (ffs_stat(fs, "/data/sub/file", &tgt) != 0)
    {
        return 1;
    }
    if (hl.st_ino != tgt.st_ino)
    {
        return 1;
    }

    ffs_destroy(fs);
    printf("persistence verified across processes\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        return 2;
    }
    if (strcmp(argv[2], "write") == 0)
    {
        return do_write(argv[1]);
    }
    if (strcmp(argv[2], "verify") == 0)
    {
        return do_verify(argv[1]);
    }
    return 2;
}
