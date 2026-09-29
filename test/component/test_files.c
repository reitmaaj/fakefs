#include "test.h"
#include "fakefs.h"

int main(void)
{
    ffs *fs;
    struct ffs_stat st;
    int fd;
    int fd2;
    char buf[64];

    fs = ffs_create_memory();
    CHECK(fs != 0);

    CHECK_ERRNO(ffs_open(fs, "/missing", O_RDONLY, 0), ENOENT);

    fd = ffs_open(fs, "/f", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "abcdef", 6), 6);

    CHECK_EQ(ffs_lseek(fs, fd, 2, SEEK_SET), 2);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, 2), 2);
    CHECK_STR(buf, "cd");

    CHECK_EQ(ffs_lseek(fs, fd, 0, SEEK_SET), 0);
    CHECK_EQ(ffs_ftruncate(fs, fd, 3), 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_stat(fs, "/f", &st), 0);
    CHECK_EQ(st.st_size, 3);

    fd = ffs_open(fs, "/f", O_RDONLY, 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, sizeof(buf)), 3);
    CHECK_STR(buf, "abc");
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    fd = ffs_open(fs, "/f", O_CREAT | O_RDWR | O_TRUNC, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_stat(fs, "/f", &st), 0);
    CHECK_EQ(st.st_size, 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_ERRNO(ffs_open(fs, "/f", O_CREAT | O_EXCL, 0644), EEXIST);

    fd = ffs_open(fs, "/g", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_ERRNO(ffs_read(fs, fd, buf, 1), EBADF);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    fd = ffs_open(fs, "/g", O_RDONLY, 0);
    CHECK(fd >= 0);
    CHECK_ERRNO(ffs_write(fs, fd, "x", 1), EBADF);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    fd = ffs_open(fs, "/app", O_CREAT | O_WRONLY | O_APPEND, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "a", 1), 1);
    CHECK_EQ(ffs_lseek(fs, fd, 0, SEEK_SET), 0);
    CHECK_EQ(ffs_write(fs, fd, "b", 1), 1);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    fd = ffs_open(fs, "/app", O_RDONLY, 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, sizeof(buf)), 2);
    CHECK_STR(buf, "ab");
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    fd = ffs_open(fs, "/hole", O_CREAT | O_RDWR, 0644);
    CHECK_EQ(ffs_write(fs, fd, "hello", 5), 5);
    CHECK_EQ(ffs_lseek(fs, fd, 10, SEEK_SET), 10);
    CHECK_EQ(ffs_write(fs, fd, "Z", 1), 1);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_stat(fs, "/hole", &st), 0);
    CHECK_EQ(st.st_size, 11);
    fd = ffs_open(fs, "/hole", O_RDONLY, 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, sizeof(buf)), 11);
    CHECK(memcmp(buf, "hello\0\0\0\0\0Z", 11) == 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    fd = ffs_open(fs, "/r", O_CREAT | O_RDWR, 0644);
    CHECK_EQ(ffs_write(fs, fd, "0123456789", 10), 10);
    fd2 = ffs_open(fs, "/r", O_RDONLY, 0);
    CHECK(fd2 >= 0);
    CHECK_EQ(ffs_lseek(fs, fd2, 4, SEEK_SET), 4);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd2, buf, 4), 4);
    CHECK_STR(buf, "4567");
    CHECK_EQ(ffs_close_fd(fs, fd2), 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_mkdir(fs, "/d", 0755), 0);
    CHECK_ERRNO(ffs_open(fs, "/d", O_RDONLY, 0), EISDIR);

    fd = ffs_open(fs, "/grow", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "ab", 2), 2);
    CHECK_EQ(ffs_ftruncate(fs, fd, 5), 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_stat(fs, "/grow", &st), 0);
    CHECK_EQ(st.st_size, 5);
    fd = ffs_open(fs, "/grow", O_RDONLY, 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, sizeof(buf)), 5);
    CHECK(memcmp(buf, "ab\0\0\0", 5) == 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_truncate(fs, "/grow", 1), 0);
    CHECK_EQ(ffs_stat(fs, "/grow", &st), 0);
    CHECK_EQ(st.st_size, 1);

    ffs_destroy(fs);
    TEST_DONE();
}
