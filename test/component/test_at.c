#include "test.h"
#include "fakefs.h"

int main(void)
{
    ffs *fs;
    struct ffs_stat st;
    int d;
    int fd;
    int f;
    char buf[16];
    long ino_f;

    fs = ffs_create_memory();
    CHECK(fs != 0);

    CHECK_EQ(ffs_mkdir(fs, "/d", 0755), 0);
    fd = ffs_open(fs, "/d/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "hi", 2), 2);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    d = ffs_open(fs, "/d", O_RDONLY | O_DIRECTORY, 0);
    CHECK(d >= 0);

    fd = ffs_openat(fs, d, "f", O_RDONLY, 0);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_read(fs, fd, buf, 2), 2);
    buf[2] = '\0';
    CHECK_STR(buf, "hi");
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    fd = ffs_openat(fs, d, "/d/f", O_RDONLY, 0);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_mkdirat(fs, d, "sub", 0755), 0);
    CHECK_EQ(ffs_fstatat(fs, d, "sub", &st, 0), 0);
    CHECK(FFS_S_ISDIR(st.st_mode));

    CHECK_EQ(ffs_stat(fs, "/d/f", &st), 0);
    ino_f = (long)st.st_ino;

    CHECK_EQ(ffs_symlinkat(fs, "f", d, "link"), 0);
    CHECK_EQ(ffs_fstatat(fs, d, "link", &st, AT_SYMLINK_NOFOLLOW), 0);
    CHECK(FFS_S_ISLNK(st.st_mode));
    CHECK_EQ(ffs_fstatat(fs, d, "link", &st, 0), 0);
    CHECK(FFS_S_ISREG(st.st_mode));
    CHECK_EQ(st.st_ino, (unsigned long)ino_f);

    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_readlinkat(fs, d, "link", buf, sizeof(buf)), 1);
    CHECK_STR(buf, "f");

    CHECK_EQ(ffs_linkat(fs, d, "f", d, "g", 0), 0);
    CHECK_EQ(ffs_stat(fs, "/d/g", &st), 0);
    CHECK_EQ(st.st_ino, (unsigned long)ino_f);

    CHECK_EQ(ffs_renameat(fs, d, "g", d, "h"), 0);
    CHECK_EQ(ffs_stat(fs, "/d/g", &st), -1);
    CHECK_EQ(ffs_stat(fs, "/d/h", &st), 0);

    CHECK_EQ(ffs_fchmodat(fs, d, "h", 0600, 0), 0);
    CHECK_EQ(ffs_stat(fs, "/d/h", &st), 0);
    CHECK_EQ(st.st_mode & 0777UL, 0600UL);

    CHECK_EQ(ffs_fchownat(fs, d, "h", 5, 6, 0), 0);
    CHECK_EQ(ffs_stat(fs, "/d/h", &st), 0);
    CHECK_EQ(st.st_uid, 5);
    CHECK_EQ(st.st_gid, 6);

    CHECK_EQ(ffs_faccessat(fs, d, "h", F_OK, 0), 0);

    CHECK_EQ(ffs_unlinkat(fs, d, "sub", AT_REMOVEDIR), 0);
    CHECK_EQ(ffs_stat(fs, "/d/sub", &st), -1);
    CHECK(errno == ENOENT);

    CHECK_EQ(ffs_unlinkat(fs, d, "link", 0), 0);
    CHECK_EQ(ffs_stat(fs, "/d/link", &st), -1);

    CHECK_EQ(ffs_chdir(fs, "/d"), 0);
    fd = ffs_openat(fs, AT_FDCWD, "f", O_RDONLY, 0);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    f = ffs_openat(fs, d, "f", O_RDONLY, 0);
    CHECK(f >= 0);
    CHECK_EQ(ffs_openat(fs, f, "x", O_RDONLY, 0), -1);
    CHECK(errno == ENOTDIR);
    CHECK_EQ(ffs_close_fd(fs, f), 0);

    CHECK_EQ(ffs_openat(fs, 999, "x", O_RDONLY, 0), -1);
    CHECK(errno == EBADF);

    CHECK_EQ(ffs_close_fd(fs, d), 0);

    CHECK_EQ(ffs_mkdir(fs, "/priv", 0600), 0);
    CHECK_EQ(ffs_access(fs, "/priv", R_OK | W_OK), 0);
    CHECK_EQ(ffs_setids(fs, 1000, 1000), 0);
    CHECK_EQ(ffs_access(fs, "/priv", R_OK), -1);
    CHECK(errno == EACCES);
    CHECK_EQ(ffs_setids(fs, 0, 0), 0);
    CHECK_EQ(ffs_access(fs, "/nonexistent", F_OK), -1);
    CHECK(errno == ENOENT);

    ffs_destroy(fs);
    TEST_DONE();
}
