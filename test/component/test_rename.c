#include "test.h"
#include "fakefs.h"

int main(void)
{
    ffs *fs;
    struct ffs_stat st;
    char buf[64];
    int fd;

    fs = ffs_create_memory();
    CHECK(fs != 0);

    fd = ffs_open(fs, "/a", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "AAA", 3), 3);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    fd = ffs_open(fs, "/b", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "BBB", 3), 3);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_rename(fs, "/a", "/b"), 0);
    CHECK_EQ(ffs_stat(fs, "/a", &st), -1);
    CHECK(errno == ENOENT);
    fd = ffs_open(fs, "/b", O_RDONLY, 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, sizeof(buf)), 3);
    CHECK_STR(buf, "AAA");
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_mkdir(fs, "/d1", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/d1/sub", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/d2", 0755), 0);

    CHECK_EQ(ffs_rename(fs, "/d1", "/d2"), 0);
    CHECK_EQ(ffs_stat(fs, "/d1", &st), -1);
    CHECK_EQ(ffs_stat(fs, "/d2/sub", &st), 0);
    CHECK(FFS_S_ISDIR(st.st_mode));

    CHECK_ERRNO(ffs_rename(fs, "/d2", "/d2/sub"), EINVAL);

    CHECK_EQ(ffs_mkdir(fs, "/empty", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/nonempty", 0755), 0);
    fd = ffs_open(fs, "/nonempty/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_ERRNO(ffs_rename(fs, "/empty", "/nonempty"), ENOTEMPTY);

    fd = ffs_open(fs, "/file", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_ERRNO(ffs_rename(fs, "/file", "/empty"), EISDIR);
    CHECK_ERRNO(ffs_rename(fs, "/empty", "/file"), ENOTDIR);

    CHECK_EQ(ffs_mkdir(fs, "/src", 0755), 0);
    CHECK_EQ(ffs_rename(fs, "/src", "/src"), 0);
    CHECK_EQ(ffs_stat(fs, "/src", &st), 0);

    CHECK_EQ(ffs_mkdir(fs, "/m1", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/m2", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/m1/child", 0755), 0);
    CHECK_EQ(ffs_rename(fs, "/m1/child", "/m2/child"), 0);
    CHECK_EQ(ffs_stat(fs, "/m2/child", &st), 0);
    CHECK_EQ(ffs_stat(fs, "/m1/child", &st), -1);

    ffs_destroy(fs);
    TEST_DONE();
}
