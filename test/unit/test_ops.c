#include "test.h"
#include "unit.h"

static void find_checks(void)
{
    CHECK_EQ(find_code(0), -EEXIST);
    CHECK_EQ(find_code(-ENOENT), 0);
    CHECK_EQ(find_code(-EACCES), -EACCES);
}

static void new_checks(void)
{
    CHECK_EQ(new_code(-EACCES, "x"), -EACCES);
    CHECK_EQ(new_code(0, ""), -ENOENT);
    CHECK_EQ(new_code(0, "x"), 0);
}

static void name_checks(void)
{
    CHECK_EQ(name_empty(""), 1);
    CHECK_EQ(name_empty("x"), 0);
}

static void type_checks(void)
{
    CHECK_EQ(type_code(1, 1), 0);
    CHECK_EQ(type_code(1, 0), -ENOTDIR);
    CHECK_EQ(type_code(0, 0), 0);
    CHECK_EQ(type_code(0, 1), -EISDIR);
}

static void skip_checks(void)
{
    CHECK_EQ(guard_skip(-EIO, 0), 1);
    CHECK_EQ(guard_skip(0, 1), 1);
    CHECK_EQ(guard_skip(0, 0), 0);
}

static void follow_checks(void)
{
    CHECK_EQ(open_follow(O_CREAT | O_EXCL), 0);
    CHECK_EQ(open_follow(O_NOFOLLOW), 0);
    CHECK_EQ(open_follow(O_CREAT | O_EXCL | O_NOFOLLOW), 0);
    CHECK_EQ(open_follow(O_CREAT), 1);
    CHECK_EQ(open_follow(O_EXCL), 1);
    CHECK_EQ(open_follow(0), 1);
}

static void excl_checks(void)
{
    CHECK_EQ(open_excl_code(1, O_CREAT | O_EXCL), -EEXIST);
    CHECK_EQ(open_excl_code(1, O_CREAT), 0);
    CHECK_EQ(open_excl_code(0, O_CREAT | O_EXCL), 0);
    CHECK_EQ(open_excl_code(0, 0), 0);
}

static void retry_checks(void)
{
    CHECK_EQ(open_retry(-ENOENT, 1), 1);
    CHECK_EQ(open_retry(-ENOENT, 0), 0);
    CHECK_EQ(open_retry(0, 1), 0);
    CHECK_EQ(open_retry(-EACCES, 1), 0);
}

static void new_bad_checks(void)
{
    CHECK_EQ(open_new_bad(1, "x", "/x"), -EINVAL);
    CHECK_EQ(open_new_bad(0, "", "/x"), -ENOENT);
    CHECK_EQ(open_new_bad(0, "x", "/x/"), -EISDIR);
    CHECK_EQ(open_new_bad(0, "x", "/x"), 0);
    CHECK_EQ(open_new_bad(0, "", ""), -ENOENT);
}

static void open_type_checks(void)
{
    CHECK_EQ(open_type_code(1, 1), 0);
    CHECK_EQ(open_type_code(1, 0), -EISDIR);
    CHECK_EQ(open_type_code(0, 1), -ENOTDIR);
    CHECK_EQ(open_type_code(0, 0), 0);
}

static void unlinked_checks(void)
{
    ffs *fs;
    struct ffs_stat st;
    long ino;
    int fd;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    fd = ffs_open(fs, "/x", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_stat(fs, "/x", &st), 0);
    ino = (long)st.st_ino;
    CHECK_EQ(unlinked_unused(fs, ino, 1), 0);
    CHECK_EQ(unlinked_unused(fs, ino, 0), 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(unlinked_unused(fs, ino, 0), 1);
    CHECK_EQ(unlinked_unused(fs, ino, 1), 0);
    ffs_destroy(fs);
}

static void rw_code_checks(void)
{
    CHECK_EQ(rw_read_code(0, O_RDONLY), 0);
    CHECK_EQ(rw_read_code(0, O_WRONLY), -EBADF);
    CHECK_EQ(rw_read_code(0, O_RDWR), 0);
    CHECK_EQ(rw_read_code(1, O_RDONLY), -EISDIR);
    CHECK_EQ(rw_read_code(1, O_WRONLY), -EISDIR);
    CHECK_EQ(rw_write_code(0, O_RDONLY), -EBADF);
    CHECK_EQ(rw_write_code(0, O_WRONLY), 0);
    CHECK_EQ(rw_write_code(0, O_RDWR), 0);
    CHECK_EQ(rw_write_code(1, O_WRONLY), -EISDIR);
    CHECK_EQ(rw_trunc_code(0, O_RDONLY), -EINVAL);
    CHECK_EQ(rw_trunc_code(0, O_WRONLY), 0);
    CHECK_EQ(rw_trunc_code(1, O_RDWR), -EISDIR);
}

static void want_checks(void)
{
    struct finode fi;

    memset(&fi, 0, sizeof(fi));
    fi.size = 10;
    CHECK_EQ(read_want(&fi, 0, 4), 4);
    CHECK_EQ(read_want(&fi, 8, 4), 2);
    CHECK_EQ(read_want(&fi, 10, 4), 0);
    CHECK_EQ(read_want(&fi, 20, 4), 0);
    CHECK_EQ(read_want(&fi, 0, 0), 0);
}

static void end_bad_checks(void)
{
    CHECK_EQ(write_end_bad(0, 0), 0);
    CHECK_EQ(write_end_bad(5, 3), 0);
    CHECK_EQ(write_end_bad(5, -1), 1);
    CHECK_EQ(write_end_bad(10, -10), 1);
}

static void seek_checks(void)
{
    CHECK_EQ(seek_valid(SEEK_SET), 0);
    CHECK_EQ(seek_valid(SEEK_CUR), 0);
    CHECK_EQ(seek_valid(SEEK_END), 0);
    CHECK_EQ(seek_valid(12345), -EINVAL);
}

static void pair_checks(void)
{
    ffs *fs;
    int fd;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    fd = ffs_open(fs, "/x", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(fd_pair_bad(fs, fd, 0), 0);
    CHECK_EQ(fd_pair_bad(fs, FFS_MAX_FD, 0), 1);
    CHECK_EQ(fd_pair_bad(fs, fd, -1), 1);
    CHECK_EQ(fd_pair_bad(fs, fd, FFS_MAX_FD), 1);
    CHECK_EQ(fd_pair_bad(0, fd, 0), 1);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    ffs_destroy(fs);
}

int main(void)
{
    find_checks();
    new_checks();
    name_checks();
    type_checks();
    skip_checks();
    follow_checks();
    excl_checks();
    retry_checks();
    new_bad_checks();
    open_type_checks();
    unlinked_checks();
    rw_code_checks();
    want_checks();
    end_bad_checks();
    seek_checks();
    pair_checks();
    TEST_DONE();
}
