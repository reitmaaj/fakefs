#include "test.h"
#include "fakefs.h"

#include <string.h>
#include <sys/time.h>

int main(void)
{
    ffs *fs;
    ffs *fs2;
    struct ffs_stat st;
    struct ffs_statfs stfs;
    struct timeval tv[2];
    char buf[16];
    char name[8];
    int fd;
    int fd2;
    int i;

    /* NULL / invalid-argument guards. */
    CHECK_NULL(ffs_create(NULL));

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    fs2 = ffs_create_memory();
    CHECK_NOTNULL(fs2);
    ffs_destroy(NULL);

    CHECK_ERRNO(ffs_setatime(NULL, 1), EINVAL);
    CHECK_ERRNO(ffs_setids(NULL, 0, 0), EINVAL);
    CHECK_ERRNO(ffs_setumask(NULL, 0), EINVAL);

    CHECK_ERRNO(ffs_mkdir(NULL, "/a", 0755), EINVAL);
    CHECK_ERRNO(ffs_mkdir(fs, NULL, 0755), EINVAL);
    CHECK_ERRNO(ffs_rmdir(NULL, "/a"), EINVAL);
    CHECK_ERRNO(ffs_unlink(NULL, "/a"), EINVAL);
    CHECK_ERRNO(ffs_link(NULL, "/a", "/b"), EINVAL);
    CHECK_ERRNO(ffs_link(fs, NULL, "/b"), EINVAL);
    CHECK_ERRNO(ffs_symlink(NULL, "t", "/l"), EINVAL);
    CHECK_ERRNO(ffs_symlink(fs, "t", NULL), EINVAL);
    CHECK_ERRNO(ffs_readlink(NULL, "/l", buf, sizeof(buf)), EINVAL);
    CHECK_ERRNO(ffs_rename(NULL, "/a", "/b"), EINVAL);
    CHECK_ERRNO(ffs_stat(NULL, "/a", &st), EINVAL);
    CHECK_ERRNO(ffs_lstat(fs, NULL, &st), EINVAL);
    CHECK_ERRNO(ffs_chmod(NULL, "/a", 0), EINVAL);
    CHECK_ERRNO(ffs_chown(NULL, "/a", 0, 0), EINVAL);
    CHECK_ERRNO(ffs_lchown(fs, NULL, 0, 0), EINVAL);
    CHECK_ERRNO(ffs_utimes(NULL, "/a", NULL), EINVAL);
    CHECK_ERRNO(ffs_truncate(NULL, "/a", 0), EINVAL);
    CHECK_ERRNO(ffs_access(NULL, "/a", F_OK), EINVAL);
    CHECK_ERRNO(ffs_chdir(NULL, "/"), EINVAL);
    CHECK_ERRNO(ffs_getcwd(NULL, buf, sizeof(buf)), EINVAL);
    CHECK_ERRNO(ffs_realpath(NULL, "/", buf, sizeof(buf)), EINVAL);
    CHECK_ERRNO(ffs_statfs(NULL, &stfs), EINVAL);
    CHECK_NULL(ffs_opendir(NULL, "/"));
    CHECK_ERRNO(ffs_readdir(NULL, name, sizeof(name)), EINVAL);
    CHECK_ERRNO(ffs_closedir(NULL), EINVAL);
    CHECK_ERRNO(ffs_open(NULL, "/a", 0, 0), EINVAL);
    CHECK_ERRNO(ffs_dup(NULL, 0), EINVAL);

    /* Setup a regular file. */
    fd = ffs_open(fs, "/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "hello", 5), 5);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    /* Invalid-fd guards (EBADF). */
    CHECK_ERRNO(ffs_close_fd(fs, -1), EBADF);
    CHECK_ERRNO(ffs_read(fs, -1, buf, sizeof(buf)), EBADF);
    CHECK_ERRNO(ffs_write(fs, -1, buf, 1), EBADF);
    CHECK_ERRNO(ffs_lseek(fs, -1, 0, SEEK_SET), EBADF);
    CHECK_ERRNO(ffs_ftruncate(fs, -1, 0), EBADF);
    CHECK_ERRNO(ffs_fstat(fs, -1, &st), EBADF);
    CHECK_ERRNO(ffs_fchmod(fs, -1, 0644), EBADF);
    CHECK_ERRNO(ffs_fchown(fs, -1, 0, 0), EBADF);
    CHECK_ERRNO(ffs_fchdir(fs, -1), EBADF);
    CHECK_ERRNO(ffs_dup(fs, -1), EBADF);
    CHECK_ERRNO(ffs_dup2(fs, -1, 5), EBADF);
    CHECK_ERRNO(ffs_dup2(fs, 5, -1), EBADF);
    CHECK_ERRNO(ffs_pread(fs, -1, buf, 1, 0), EBADF);
    CHECK_ERRNO(ffs_pwrite(fs, -1, buf, 1, 0), EBADF);
    CHECK_ERRNO(ffs_fcntl(fs, -1, F_GETFL, 0), EBADF);

    /* NULL buffer guards (EINVAL). */
    fd = ffs_open(fs, "/f", O_RDONLY, 0);
    CHECK(fd >= 0);
    CHECK_ERRNO(ffs_read(fs, fd, NULL, 1), EINVAL);
    CHECK_ERRNO(ffs_write(fs, fd, NULL, 1), EINVAL);
    CHECK_ERRNO(ffs_pread(fs, fd, NULL, 1, 0), EINVAL);
    CHECK_ERRNO(ffs_pwrite(fs, fd, NULL, 1, 0), EINVAL);

    /* read at EOF returns 0. */
    CHECK_EQ(ffs_lseek(fs, fd, 0, SEEK_END), 5);
    CHECK_EQ(ffs_read(fs, fd, buf, sizeof(buf)), 0);
    CHECK_EQ(ffs_pread(fs, fd, buf, sizeof(buf), 100), 0);

    /* lseek EINVAL cases. */
    CHECK_ERRNO(ffs_lseek(fs, fd, -1, SEEK_SET), EINVAL);
    CHECK_ERRNO(ffs_lseek(fs, fd, 0, 999), EINVAL);

    /* pwrite on a read-only fd -> EBADF. */
    CHECK_ERRNO(ffs_pwrite(fs, fd, "x", 1, 0), EBADF);

    /* pwrite grows the file; zero-length pwrite is a no-op. */
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    fd = ffs_open(fs, "/f", O_RDWR, 0);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_pwrite(fs, fd, "xy", 2, 10), 2);
    CHECK_EQ(ffs_pwrite(fs, fd, buf, 0, 0), 0);

    /* dup2 same-fd and replace-existing. */
    CHECK_EQ(ffs_dup2(fs, fd, fd), fd);
    fd2 = ffs_dup(fs, fd);
    CHECK(fd2 >= 0);
    CHECK_EQ(ffs_dup2(fs, fd, fd2), fd2);
    CHECK_EQ(ffs_close_fd(fs, fd2), 0);

    /* fcntl F_DUPFD with negative start (clamps to 0) and bad cmd. */
    fd2 = ffs_fcntl(fs, fd, F_DUPFD, -5);
    CHECK(fd2 >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd2), 0);
    CHECK_ERRNO(ffs_fcntl(fs, fd, 999, 0), EINVAL);

    /* ftruncate on a read-only fd -> EINVAL; on a directory -> EISDIR. */
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    fd = ffs_open(fs, "/f", O_RDONLY, 0);
    CHECK(fd >= 0);
    CHECK_ERRNO(ffs_ftruncate(fs, fd, 0), EINVAL);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_mkdir(fs, "/d", 0755), 0);
    fd = ffs_open(fs, "/d", O_RDONLY | O_DIRECTORY, 0);
    CHECK(fd >= 0);
    CHECK_ERRNO(ffs_ftruncate(fs, fd, 0), EISDIR);
    CHECK_ERRNO(ffs_read(fs, fd, buf, 1), EISDIR);
    CHECK_ERRNO(ffs_write(fs, fd, "x", 1), EISDIR);
    CHECK_ERRNO(ffs_pread(fs, fd, buf, 1, 0), EISDIR);
    CHECK_ERRNO(ffs_pwrite(fs, fd, "x", 1, 0), EISDIR);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    /* fchdir on a regular file -> ENOTDIR. */
    fd2 = ffs_open(fs, "/f", O_RDONLY, 0);
    CHECK(fd2 >= 0);
    CHECK_ERRNO(ffs_fchdir(fs, fd2), ENOTDIR);
    CHECK_EQ(ffs_close_fd(fs, fd2), 0);

    /* opendir/readdir/closedir error cases. */
    CHECK_NULL(ffs_opendir(fs, "/f"));
    CHECK(errno == ENOTDIR);
    {
        ffs_dir *d = ffs_opendir(fs, "/d");
        CHECK_NOTNULL(d);
        CHECK_ERRNO(ffs_readdir(d, name, 1), ENAMETOOLONG);
        CHECK_EQ(ffs_closedir(d), 0);
    }

    /* unlink/rmdir edge cases. */
    CHECK_ERRNO(ffs_unlink(fs, "/"), EINVAL);
    CHECK_ERRNO(ffs_unlink(fs, "/d"), EISDIR);
    CHECK_ERRNO(ffs_link(fs, "/f", "/f"), EEXIST);
    CHECK_ERRNO(ffs_symlink(fs, "t", "/f"), EEXIST);
    CHECK_EQ(ffs_mkdir(fs, "/nested", 0755), 0);
    fd = ffs_open(fs, "/nested/x", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_ERRNO(ffs_rmdir(fs, "/nested"), ENOTEMPTY);
    CHECK_ERRNO(ffs_rename(fs, "/", "/x"), EINVAL);
    CHECK_ERRNO(ffs_realpath(fs, "/nope", buf, sizeof(buf)), ENOENT);

    /* Permission matrix (non-root identity). */
    CHECK_EQ(ffs_mkdir(fs, "/owned", 0755), 0);
    fd = ffs_open(fs, "/owned/t", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_chown(fs, "/owned/t", 1000, 2000), 0);

    /* utimes as owner. */
    CHECK_EQ(ffs_setids(fs, 1000, 2000), 0);
    tv[0].tv_sec = 1;
    tv[0].tv_usec = 0;
    tv[1].tv_sec = 1;
    tv[1].tv_usec = 0;
    CHECK_EQ(ffs_utimes(fs, "/owned/t", tv), 0);

    /* utimes as non-owner with write permission. */
    CHECK_EQ(ffs_setids(fs, 0, 0), 0);
    CHECK_EQ(ffs_chown(fs, "/owned/t", 0, 0), 0);
    CHECK_EQ(ffs_chmod(fs, "/owned/t", 0666), 0);
    CHECK_EQ(ffs_setids(fs, 1000, 2000), 0);
    CHECK_EQ(ffs_utimes(fs, "/owned/t", tv), 0);

    /* utimes as non-owner without write -> EPERM. */
    CHECK_EQ(ffs_setids(fs, 0, 0), 0);
    CHECK_EQ(ffs_chmod(fs, "/owned/t", 0644), 0);
    CHECK_EQ(ffs_setids(fs, 1000, 2000), 0);
    CHECK_ERRNO(ffs_utimes(fs, "/owned/t", tv), EPERM);

    /* open O_RDONLY|O_TRUNC on an unwritable file -> EACCES. */
    CHECK_EQ(ffs_setids(fs, 0, 0), 0);
    CHECK_EQ(ffs_chmod(fs, "/owned/t", 0444), 0);
    CHECK_EQ(ffs_setids(fs, 1000, 2000), 0);
    CHECK_ERRNO(ffs_open(fs, "/owned/t", O_RDONLY | O_TRUNC, 0), EACCES);

    /* ffs_access W_OK and X_OK failures. */
    CHECK_ERRNO(ffs_access(fs, "/owned/t", W_OK), EACCES);
    CHECK_EQ(ffs_setids(fs, 0, 0), 0);
    CHECK_EQ(ffs_mkdir(fs, "/nx", 0000), 0);
    CHECK_EQ(ffs_setids(fs, 1000, 2000), 0);
    CHECK_ERRNO(ffs_access(fs, "/nx", X_OK), EACCES);

    /* fd exhaustion: of table full -> EMFILE. */
    fd = ffs_open(fs2, "/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs2, fd), 0);
    for (i = 0; i < 64; i++)
    {
        CHECK(ffs_open(fs2, "/f", O_RDONLY, 0) >= 0);
    }
    CHECK_ERRNO(ffs_open(fs2, "/f", O_RDONLY, 0), EMFILE);

    /* dup exhaustion: fd table full -> EMFILE. */
    {
        ffs *fs3 = ffs_create_memory();
        int d;

        CHECK_NOTNULL(fs3);
        fd = ffs_open(fs3, "/f", O_CREAT | O_WRONLY, 0644);
        CHECK(fd >= 0);
        CHECK_EQ(ffs_close_fd(fs3, fd), 0);
        fd = ffs_open(fs3, "/f", O_RDONLY, 0);
        CHECK(fd >= 0);
        for (i = 0; i < 63; i++)
        {
            d = ffs_dup(fs3, fd);
            CHECK(d >= 0);
        }
        CHECK_ERRNO(ffs_dup(fs3, fd), EMFILE);
        CHECK_ERRNO(ffs_fcntl(fs3, fd, F_DUPFD, 0), EMFILE);
        ffs_destroy(fs3);
    }

    ffs_destroy(fs);
    ffs_destroy(fs2);
    TEST_DONE();
}
