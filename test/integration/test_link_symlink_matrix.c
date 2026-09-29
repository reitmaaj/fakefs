#include "test.h"
#include "fakefs.h"

int main(void)
{
    ffs *fs;
    struct ffs_stat st;
    struct ffs_stat lst;
    char buf[32];
    long ino;
    int fd;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);

    fd = ffs_open(fs, "/x", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "data", 4), 4);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_stat(fs, "/x", &st), 0);
    ino = (long)st.st_ino;

    /* hard link shares inode + nlink */
    CHECK_EQ(ffs_link(fs, "/x", "/y"), 0);
    CHECK_EQ(ffs_stat(fs, "/y", &st), 0);
    CHECK_EQ((long)st.st_ino, ino);
    CHECK_EQ(st.st_nlink, 2);

    /* unlink a peer: the other survives */
    CHECK_EQ(ffs_unlink(fs, "/x"), 0);
    CHECK_EQ(ffs_stat(fs, "/y", &st), 0);
    CHECK_EQ(st.st_nlink, 1);

    /* link to a directory is rejected */
    CHECK_EQ(ffs_mkdir(fs, "/d", 0755), 0);
    CHECK_ERRNO(ffs_link(fs, "/d", "/z"), EPERM);

    /* symlink chain a -> b -> file */
    CHECK_EQ(ffs_symlink(fs, "b", "/a"), 0);
    CHECK_EQ(ffs_symlink(fs, "y", "/b"), 0);
    CHECK_EQ(ffs_stat(fs, "/a", &st), 0);
    CHECK(FFS_S_ISREG(st.st_mode));
    CHECK_EQ(st.st_size, 4);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_readlink(fs, "/a", buf, sizeof(buf)), 1);
    CHECK_STR(buf, "b");

    /* symlink loop terminates with ELOOP */
    CHECK_EQ(ffs_symlink(fs, "loop2", "/loop1"), 0);
    CHECK_EQ(ffs_symlink(fs, "loop1", "/loop2"), 0);
    CHECK_ERRNO(ffs_stat(fs, "/loop1", &st), ELOOP);

    /* dangling symlink: stat ENOENT, lstat shows a link */
    CHECK_EQ(ffs_symlink(fs, "nothing", "/dang"), 0);
    CHECK_ERRNO(ffs_stat(fs, "/dang", &st), ENOENT);
    CHECK_EQ(ffs_lstat(fs, "/dang", &lst), 0);
    CHECK(FFS_S_ISLNK(lst.st_mode));

    ffs_destroy(fs);
    TEST_DONE();
}
