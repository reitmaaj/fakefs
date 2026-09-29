#include "test.h"
#include "fakefs.h"

#include <errno.h>

static void test_realpath_after_file_rename(void)
{
    ffs *fs;
    struct ffs_stat st;
    char buf[128];
    int fd;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    CHECK_EQ(ffs_mkdir(fs, "/a", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/b", 0755), 0);
    fd = ffs_open(fs, "/a/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_rename(fs, "/a/f", "/b/f"), 0);
    CHECK_EQ(ffs_realpath(fs, "/b/f", buf, sizeof(buf)), 0);
    CHECK_STR(buf, "/b/f");
    CHECK_EQ(ffs_stat(fs, "/a/f", &st), -1);
    CHECK(errno == ENOENT);
    ffs_destroy(fs);
}

static void test_symlink_rename_preserves_target(void)
{
    ffs *fs;
    struct ffs_stat st;
    char target[128];
    ssize_t n;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    CHECK_EQ(ffs_mkdir(fs, "/a", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/b", 0755), 0);
    CHECK_EQ(ffs_symlink(fs, "tgt", "/a/l"), 0);

    CHECK_EQ(ffs_rename(fs, "/a/l", "/b/l"), 0);
    memset(target, 0, sizeof(target));
    n = ffs_readlink(fs, "/b/l", target, sizeof(target));
    CHECK_EQ((long)n, 3);
    CHECK_STR(target, "tgt");
    CHECK_EQ(ffs_stat(fs, "/a/l", &st), -1);
    CHECK(errno == ENOENT);
    ffs_destroy(fs);
}

static void test_dup2_frees_unlinked_inode(void)
{
    ffs *fs;
    struct ffs_statfs stfs;
    int fd0;
    int fd1;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    fd0 = ffs_open(fs, "/f", O_CREAT | O_RDWR, 0644);
    CHECK(fd0 >= 0);
    CHECK_EQ(ffs_unlink(fs, "/f"), 0);
    fd1 = ffs_open(fs, "/g", O_CREAT | O_RDWR, 0644);
    CHECK(fd1 >= 0);

    CHECK_EQ(ffs_dup2(fs, fd1, fd0), fd0);
    CHECK_EQ(ffs_close_fd(fs, fd1), 0);
    CHECK_EQ(ffs_statfs(fs, &stfs), 0);
    CHECK_EQ((long)stfs.f_files, 2);
    ffs_destroy(fs);
}

static void test_open_creat_directory_missing_path(void)
{
    ffs *fs;
    struct ffs_stat st;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    CHECK_ERRNO(ffs_open(fs, "/newdir", O_CREAT | O_DIRECTORY, 0700), EINVAL);
    CHECK_EQ(ffs_stat(fs, "/newdir", &st), -1);
    CHECK(errno == ENOENT);
    ffs_destroy(fs);
}

static void test_open_trunc_emfile_no_truncate(void)
{
    ffs *fs;
    struct ffs_stat st;
    char buf[16];
    int fds[64];
    int n;
    int fd;
    int i;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    fd = ffs_open(fs, "/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ((int)ffs_write(fs, fd, "KEEPME", 6), 6);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    n = 0;
    for (i = 0; i < 64; i++)
    {
        fds[n] = ffs_open(fs, "/hold", O_CREAT | O_RDWR, 0644);
        CHECK(fds[n] >= 0);
        n++;
    }

    CHECK_ERRNO(ffs_open(fs, "/f", O_RDONLY | O_TRUNC, 0), EMFILE);
    CHECK_EQ(ffs_stat(fs, "/f", &st), 0);
    CHECK_EQ((long)st.st_size, 6);

    for (i = 0; i < n; i++)
    {
        CHECK_EQ(ffs_close_fd(fs, fds[i]), 0);
    }
    memset(buf, 0, sizeof(buf));
    fd = ffs_open(fs, "/f", O_RDONLY, 0);
    CHECK(fd >= 0);
    CHECK_EQ((int)ffs_read(fs, fd, buf, sizeof(buf) - 1), 6);
    CHECK_STR(buf, "KEEPME");
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    ffs_destroy(fs);
}

static void test_realpath_remaining_hardlink(void)
{
    ffs *fs;
    struct ffs_stat st;
    char buf[128];

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    CHECK_EQ(ffs_mkdir(fs, "/a", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/b", 0755), 0);
    CHECK_EQ(ffs_open(fs, "/a/f", O_CREAT | O_WRONLY, 0644) >= 0, 1);
    CHECK_EQ(ffs_link(fs, "/a/f", "/b/f"), 0);
    CHECK_EQ(ffs_unlink(fs, "/a/f"), 0);

    CHECK_EQ(ffs_realpath(fs, "/b/f", buf, sizeof(buf)), 0);
    CHECK_STR(buf, "/b/f");
    CHECK_EQ(ffs_stat(fs, "/a/f", &st), -1);
    CHECK(errno == ENOENT);
    ffs_destroy(fs);
}

static void test_pread_updates_atime(void)
{
    ffs *fs;
    struct ffs_stat st;
    struct timeval tv[2];
    char buf[8];
    int fd;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    CHECK_EQ(ffs_setatime(fs, 1), 0);
    fd = ffs_open(fs, "/f", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ((int)ffs_write(fs, fd, "hello", 5), 5);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    tv[0].tv_sec = 1000;
    tv[0].tv_usec = 0;
    tv[1].tv_sec = 2000;
    tv[1].tv_usec = 0;
    CHECK_EQ(ffs_utimes(fs, "/f", tv), 0);

    fd = ffs_open(fs, "/f", O_RDONLY, 0);
    CHECK(fd >= 0);
    CHECK_EQ((int)ffs_pread(fs, fd, buf, sizeof(buf), 0), 5);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_stat(fs, "/f", &st), 0);
    CHECK(st.st_atime > 1000);
    ffs_destroy(fs);
}

static void test_open_creat_trailing_slash(void)
{
    ffs *fs;
    struct ffs_stat st;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    CHECK_ERRNO(ffs_open(fs, "/miss/", O_CREAT | O_WRONLY, 0644), EISDIR);
    CHECK_EQ(ffs_stat(fs, "/miss", &st), -1);
    CHECK(errno == ENOENT);
    CHECK_ERRNO(ffs_open(fs, "/miss/", O_CREAT | O_RDONLY, 0644), EISDIR);

    CHECK_EQ(ffs_symlink(fs, "gone", "/dang"), 0);
    CHECK_ERRNO(ffs_open(fs, "/dang/", O_CREAT | O_WRONLY, 0644), EISDIR);
    CHECK_EQ(ffs_stat(fs, "/gone", &st), -1);
    CHECK(errno == ENOENT);
    ffs_destroy(fs);
}

static void test_lstat_trailing_slash_follows(void)
{
    ffs *fs;
    struct ffs_stat st;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    CHECK_EQ(ffs_mkdir(fs, "/d", 0755), 0);
    CHECK_EQ(ffs_symlink(fs, "d", "/l"), 0);

    CHECK_EQ(ffs_lstat(fs, "/l", &st), 0);
    CHECK(FFS_S_ISLNK(st.st_mode));
    CHECK_EQ(ffs_lstat(fs, "/l/", &st), 0);
    CHECK(FFS_S_ISDIR(st.st_mode));
    ffs_destroy(fs);
}

static void test_destructive_op_trailing_slash_symlink(void)
{
    ffs *fs;
    struct ffs_stat st;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    CHECK_EQ(ffs_mkdir(fs, "/d", 0755), 0);
    CHECK_EQ(ffs_symlink(fs, "d", "/l"), 0);

    CHECK_ERRNO(ffs_unlink(fs, "/l/"), ENOTDIR);
    CHECK_ERRNO(ffs_rmdir(fs, "/l/"), ENOTDIR);
    CHECK_EQ(ffs_stat(fs, "/d", &st), 0);
    CHECK(FFS_S_ISDIR(st.st_mode));
    CHECK_EQ(ffs_lstat(fs, "/l", &st), 0);
    CHECK(FFS_S_ISLNK(st.st_mode));
    ffs_destroy(fs);
}

static void test_name_length_boundaries(void)
{
    ffs *fs;
    char n255[300];
    char n256[300];
    char p[600];
    struct ffs_stat st;
    int fd;
    int i;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    memset(n255, 'x', 255);
    n255[255] = '\0';
    memset(n256, 'x', 256);
    n256[256] = '\0';

    CHECK_EQ(ffs_mkdir(fs, n255, 0755), 0);
    CHECK_ERRNO(ffs_mkdir(fs, n256, 0755), ENAMETOOLONG);

    sprintf(p, "/%s/", n255);
    for (i = 0; i < 255; i++)
    {
        p[257 + i] = 'y';
    }
    p[512] = '\0';
    fd = ffs_open(fs, p, O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_stat(fs, p, &st), 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    ffs_destroy(fs);
}

int main(void)
{
    test_realpath_after_file_rename();
    test_symlink_rename_preserves_target();
    test_dup2_frees_unlinked_inode();
    test_open_creat_directory_missing_path();
    test_open_trunc_emfile_no_truncate();
    test_realpath_remaining_hardlink();
    test_pread_updates_atime();
    test_open_creat_trailing_slash();
    test_lstat_trailing_slash_follows();
    test_destructive_op_trailing_slash_symlink();
    test_name_length_boundaries();
    TEST_DONE();
}
