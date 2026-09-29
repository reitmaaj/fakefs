#include "test.h"
#include "unit.h"

static void helper_checks(void)
{
    struct ffs fs;

    memset(&fs, 0, sizeof(fs));
    CHECK_EQ(of_slot_match(&fs, 0, 7), 0);
    CHECK_EQ(of_taken(&fs, 0), 0);
    CHECK_EQ(fd_taken(&fs, 0), 0);
    fs.ofs[0].used = 1;
    fs.ofs[0].ino = 7;
    fs.fds[0].used = 1;
    CHECK_EQ(of_slot_match(&fs, 0, 7), 1);
    CHECK_EQ(of_slot_match(&fs, 0, 8), 0);
    CHECK_EQ(of_taken(&fs, 0), 1);
    CHECK_EQ(fd_taken(&fs, 0), 1);
    CHECK_EQ(of_taken(&fs, FFS_MAX_FD), 0);
    CHECK_EQ(fd_taken(&fs, FFS_MAX_FD), 0);
    CHECK_EQ(slot_or_err(0), 0);
    CHECK_EQ(slot_or_err(FFS_MAX_FD), -EMFILE);
    CHECK_EQ(fd_min(-5), 0);
    CHECK_EQ(fd_min(3), 3);
}

int main(void)
{
    struct ffs fs;
    int i;
    int of;
    int fd;

    memset(&fs, 0, sizeof(fs));
    helper_checks();

    of = of_alloc(&fs, 10, O_RDONLY, 0);
    CHECK_EQ(of, 0);
    CHECK_EQ(fs.ofs[0].used, 1);
    CHECK_EQ(fs.ofs[0].ino, 10);
    CHECK_EQ(fs.ofs[0].offset, 0);
    CHECK_EQ(fs.ofs[0].flags, O_RDONLY);
    CHECK_EQ(fs.ofs[0].is_dir, 0);
    CHECK_EQ(fs.ofs[0].refs, 1);

    for (i = 1; i < FFS_MAX_FD; i++)
    {
        of_alloc(&fs, i, 0, 0);
    }
    CHECK_EQ(of_alloc(&fs, 1, 0, 0), -EMFILE);

    memset(&fs.ofs, 0, sizeof(fs.ofs));
    of = of_alloc(&fs, 20, O_WRONLY, 0);
    fd = fd_alloc2(&fs, of, 0);
    CHECK_EQ(fd, 0);
    CHECK_EQ(fs.fds[0].used, 1);
    CHECK_EQ(fs.fds[0].of, of);
    fd = fd_alloc2(&fs, of, 0);
    CHECK_EQ(fd, 1);

    memset(&fs.fds, 0, sizeof(fs.fds));
    CHECK_EQ(fd_alloc2(&fs, of, 5), 5);

    memset(&fs.fds, 0, sizeof(fs.fds));
    memset(&fs.ofs, 0, sizeof(fs.ofs));
    of = of_alloc(&fs, 30, O_RDWR, 0);
    CHECK_EQ(fd_alloc2(&fs, of, 0), 0);
    CHECK_EQ(dup_min(&fs, 0, 0), 1);
    CHECK_EQ(fs.fds[1].of, of);
    CHECK_EQ(fs.ofs[of].refs, 2);
    CHECK_EQ(dup_min(&fs, 0, 5), 5);
    CHECK_EQ(fs.ofs[of].refs, 3);
    CHECK_EQ(dup_min(&fs, 999, 0), -EBADF);

    CHECK_EQ(ino_fd_refs(&fs, 30), 1);
    CHECK_EQ(ino_fd_refs(&fs, 31), 0);

    TEST_DONE();
}
