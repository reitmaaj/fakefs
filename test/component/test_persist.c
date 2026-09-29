#include "test.h"
#include "fakefs.h"
#include <unistd.h>

int main(void)
{
    ffs *fs;
    struct ffs_stat st;
    char buf[64];
    const char *path;
    int fd;

    path = "/tmp/opencode/fakefs_persist_test.db";
    unlink(path);

    fs = ffs_create(path);
    CHECK(fs != 0);
    CHECK_EQ(ffs_mkdir(fs, "/dir", 0755), 0);
    fd = ffs_open(fs, "/dir/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "persisted", 9), 9);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    ffs_destroy(fs);

    fs = ffs_create(path);
    CHECK(fs != 0);
    CHECK_EQ(ffs_stat(fs, "/dir/f", &st), 0);
    CHECK_EQ(st.st_size, 9);
    fd = ffs_open(fs, "/dir/f", O_RDONLY, 0);
    CHECK(fd >= 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, sizeof(buf)), 9);
    CHECK_STR(buf, "persisted");
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    ffs_destroy(fs);

    unlink(path);
    TEST_DONE();
}
