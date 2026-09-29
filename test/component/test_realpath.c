#include "test.h"
#include "fakefs.h"

int main(void)
{
    ffs *fs;
    char buf[256];
    int fd;

    fs = ffs_create_memory();
    CHECK(fs != 0);

    CHECK_EQ(ffs_mkdir(fs, "/a", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/a/b", 0755), 0);
    fd = ffs_open(fs, "/a/b/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_realpath(fs, "/a/b/../b/./f", buf, sizeof(buf)), 0);
    CHECK_STR(buf, "/a/b/f");

    CHECK_EQ(ffs_symlink(fs, "/a/b", "/link"), 0);
    CHECK_EQ(ffs_realpath(fs, "/link/f", buf, sizeof(buf)), 0);
    CHECK_STR(buf, "/a/b/f");

    CHECK_EQ(ffs_realpath(fs, "/", buf, sizeof(buf)), 0);
    CHECK_STR(buf, "/");

    CHECK_ERRNO(ffs_realpath(fs, "/nope", buf, sizeof(buf)), ENOENT);

    ffs_destroy(fs);
    TEST_DONE();
}
