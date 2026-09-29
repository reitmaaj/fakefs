#include "test.h"
#include "fakefs.h"

int main(void)
{
    ffs *fs1;
    ffs *fs2;
    struct ffs_stat st;
    char buf[64];
    int fd;

    fs1 = ffs_create_memory();
    fs2 = ffs_create_memory();
    CHECK_NOTNULL(fs1);
    CHECK_NOTNULL(fs2);

    CHECK_EQ(ffs_mkdir(fs1, "/one", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs2, "/two", 0755), 0);

    fd = ffs_open(fs1, "/one/only1", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs1, fd, "first", 5), 5);
    CHECK_EQ(ffs_close_fd(fs1, fd), 0);

    /* fs2 does not see fs1's tree */
    CHECK_EQ(ffs_stat(fs2, "/one", &st), -1);
    CHECK(errno == ENOENT);
    CHECK_EQ(ffs_stat(fs1, "/two", &st), -1);
    CHECK(errno == ENOENT);

    /* independent identity state */
    CHECK_EQ(ffs_setids(fs1, 1000, 1000), 0);
    fd = ffs_open(fs1, "/one/only1", O_RDONLY, 0);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs1, fd), 0);
    CHECK_EQ(ffs_stat(fs2, "/two", &st), 0);

    /* independent cwd */
    CHECK_EQ(ffs_chdir(fs1, "/one"), 0);
    CHECK_EQ(ffs_getcwd(fs1, buf, sizeof(buf)), 0);
    CHECK_STR(buf, "/one");
    CHECK_EQ(ffs_getcwd(fs2, buf, sizeof(buf)), 0);
    CHECK_STR(buf, "/");

    /* fds are per-handle */
    fd = ffs_open(fs2, "/two/f2", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs2, fd), 0);
    CHECK_ERRNO(ffs_fstat(fs1, 0, &st), EBADF);

    ffs_destroy(fs1);
    ffs_destroy(fs2);
    TEST_DONE();
}
