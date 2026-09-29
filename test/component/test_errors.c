#include "test.h"
#include "fakefs.h"

int main(void)
{
    ffs *fs;
    struct ffs_stat st;
    char longname[300];
    char buf[64];
    int fd;
    int i;

    fs = ffs_create_memory();
    CHECK(fs != 0);

    CHECK_ERRNO(ffs_stat(fs, "/nope", &st), ENOENT);
    CHECK_ERRNO(ffs_lstat(fs, "/nope", &st), ENOENT);
    CHECK_ERRNO(ffs_chdir(fs, "/nope"), ENOENT);

    CHECK_EQ(ffs_mkdir(fs, "/d", 0755), 0);
    CHECK_ERRNO(ffs_mkdir(fs, "/d", 0755), EEXIST);
    CHECK_ERRNO(ffs_rmdir(fs, "/missing"), ENOENT);
    CHECK_ERRNO(ffs_unlink(fs, "/d"), EISDIR);

    fd = ffs_open(fs, "/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_ERRNO(ffs_rmdir(fs, "/f"), ENOTDIR);
    CHECK_ERRNO(ffs_chdir(fs, "/f"), ENOTDIR);

    CHECK_EQ(ffs_symlink(fs, "/loop", "/loop"), 0);
    CHECK_ERRNO(ffs_stat(fs, "/loop", &st), ELOOP);

    for (i = 0; i < 290; i++)
    {
        longname[i] = 'a';
    }
    longname[290] = '\0';
    CHECK_ERRNO(ffs_mkdir(fs, longname, 0755), ENAMETOOLONG);

    fd = ffs_open(fs, "/u", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "keep", 4), 4);
    CHECK_EQ(ffs_unlink(fs, "/u"), 0);
    CHECK_EQ(ffs_stat(fs, "/u", &st), -1);
    CHECK(errno == ENOENT);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_lseek(fs, fd, 0, SEEK_SET), 0);
    CHECK_EQ(ffs_read(fs, fd, buf, sizeof(buf)), 4);
    CHECK_STR(buf, "keep");
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    ffs_destroy(fs);
    TEST_DONE();
}
