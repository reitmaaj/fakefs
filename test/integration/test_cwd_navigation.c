#include "test.h"
#include "fakefs.h"

int main(void)
{
    ffs *fs;
    struct ffs_stat st;
    char buf[4096];
    int fd;
    int dirfd;
    int subfd;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);

    CHECK_EQ(ffs_mkdir(fs, "/a", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/a/b", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/a/b/c", 0755), 0);
    fd = ffs_open(fs, "/a/b/c/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_chdir(fs, "/a/b"), 0);
    CHECK_EQ(ffs_getcwd(fs, buf, sizeof(buf)), 0);
    CHECK_STR(buf, "/a/b");

    /* relative resolution from cwd */
    CHECK_EQ(ffs_stat(fs, "c/f", &st), 0);
    CHECK(FFS_S_ISREG(st.st_mode));
    CHECK_EQ(ffs_stat(fs, "..", &st), 0);
    CHECK(FFS_S_ISDIR(st.st_mode));
    CHECK_EQ(ffs_stat(fs, "../b", &st), 0);
    CHECK(FFS_S_ISDIR(st.st_mode));

    /* fchdir via a directory fd */
    dirfd = ffs_open(fs, "/a/b/c", O_RDONLY | O_DIRECTORY, 0);
    CHECK(dirfd >= 0);
    CHECK_EQ(ffs_fchdir(fs, dirfd), 0);
    CHECK_EQ(ffs_getcwd(fs, buf, sizeof(buf)), 0);
    CHECK_STR(buf, "/a/b/c");
    CHECK_EQ(ffs_close_fd(fs, dirfd), 0);

    /* AT_FDCWD resolves relative to cwd */
    fd = ffs_openat(fs, AT_FDCWD, "f", O_RDONLY, 0);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    /* directory fd capability: descend without path prefix */
    subfd = ffs_open(fs, "/a", O_RDONLY | O_DIRECTORY, 0);
    CHECK(subfd >= 0);
    CHECK_EQ(ffs_fstatat(fs, subfd, "b/c/f", &st, 0), 0);
    CHECK(FFS_S_ISREG(st.st_mode));
    CHECK_EQ(ffs_close_fd(fs, subfd), 0);

    /* realpath canonicalizes relative to cwd */
    CHECK_EQ(ffs_realpath(fs, "../c/f", buf, sizeof(buf)), 0);
    CHECK_STR(buf, "/a/b/c/f");

    ffs_destroy(fs);
    TEST_DONE();
}
