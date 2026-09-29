#include "test.h"
#include "fakefs.h"

int main(void)
{
    ffs *fs;
    struct ffs_statfs sf;
    int fd;
    unsigned long before;

    fs = ffs_create_memory();
    CHECK(fs != 0);

    CHECK_EQ(ffs_statfs(fs, &sf), 0);
    CHECK_EQ(sf.f_bsize, 1);
    CHECK_EQ(sf.f_ffree, 0);
    CHECK_EQ(sf.f_bfree, 0);
    before = sf.f_files;

    fd = ffs_open(fs, "/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "hello", 5), 5);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_statfs(fs, &sf), 0);
    CHECK(sf.f_files >= before + 1);
    CHECK(sf.f_blocks >= 5);

    CHECK_EQ(ffs_unlink(fs, "/f"), 0);
    CHECK_EQ(ffs_statfs(fs, &sf), 0);
    CHECK(sf.f_files <= before + 1);

    ffs_destroy(fs);
    TEST_DONE();
}
