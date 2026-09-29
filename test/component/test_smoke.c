#include "test.h"
#include "fakefs.h"

int main(void)
{
    ffs *fs;
    struct ffs_stat st;
    int fd;
    char buf[64];

    fs = ffs_create_memory();
    CHECK(fs != 0);

    CHECK_EQ(ffs_stat(fs, "/", &st), 0);
    CHECK(FFS_S_ISDIR(st.st_mode));
    CHECK_EQ(st.st_ino, 1);

    CHECK_EQ(ffs_mkdir(fs, "/home", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/home/u", 0700), 0);

    fd = ffs_open(fs, "/home/u/a.txt", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "hello", 5), 5);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_stat(fs, "/home/u/a.txt", &st), 0);
    CHECK_EQ(st.st_size, 5);
    CHECK(FFS_S_ISREG(st.st_mode));
    CHECK(FFS_S_ISDIR(0x4000 | 0755));

    fd = ffs_open(fs, "/home/u/a.txt", O_RDONLY, 0);
    CHECK(fd >= 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, sizeof(buf)), 5);
    CHECK_STR(buf, "hello");
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_unlink(fs, "/home/u/a.txt"), 0);
    CHECK_EQ(ffs_stat(fs, "/home/u/a.txt", &st), -1);
    CHECK(errno == ENOENT);

    ffs_destroy(fs);
    TEST_DONE();
}
