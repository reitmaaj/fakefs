#include "test.h"
#include "fakefs.h"

/*
 * Permission matrix: acting identity (owner/group/other/root) x operation
 * (read/write/execute) x object (file/dir). Asserts EACCES vs success.
 */

int main(void)
{
    ffs *fs;
    int fd;
    struct ffs_stat st;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);

    fd = ffs_open(fs, "/f", O_CREAT | O_WRONLY, 0640);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_chown(fs, "/f", 1000, 2000), 0);

    CHECK_EQ(ffs_mkdir(fs, "/d", 0700), 0);
    CHECK_EQ(ffs_chown(fs, "/d", 1000, 2000), 0);
    fd = ffs_open(fs, "/d/child", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    /* owner (1000,2000): read + write file, search dir */
    CHECK_EQ(ffs_setids(fs, 1000, 2000), 0);
    fd = ffs_open(fs, "/f", O_RDONLY, 0);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    fd = ffs_open(fs, "/f", O_WRONLY, 0);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_stat(fs, "/d/child", &st), 0);

    /* group (1001,2000): file mode 0640 -> group r-- : read ok, write EACCES */
    CHECK_EQ(ffs_setids(fs, 1001, 2000), 0);
    fd = ffs_open(fs, "/f", O_RDONLY, 0);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_ERRNO(ffs_open(fs, "/f", O_WRONLY, 0), EACCES);

    /* other (1001,9999): no bits -> read and write EACCES; dir search EACCES */
    CHECK_EQ(ffs_setids(fs, 1001, 9999), 0);
    CHECK_ERRNO(ffs_open(fs, "/f", O_RDONLY, 0), EACCES);
    CHECK_ERRNO(ffs_open(fs, "/f", O_WRONLY, 0), EACCES);
    CHECK_ERRNO(ffs_stat(fs, "/d/child", &st), EACCES);

    /* root bypasses read/write */
    CHECK_EQ(ffs_setids(fs, 0, 0), 0);
    fd = ffs_open(fs, "/f", O_RDONLY, 0);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    fd = ffs_open(fs, "/f", O_WRONLY, 0);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    ffs_destroy(fs);
    TEST_DONE();
}
