#include "test.h"
#include "fakefs.h"

int main(void)
{
    ffs *fs;
    struct ffs_stat st;
    char buf[64];
    char cwd[256];
    int fd;
    int d2;

    fs = ffs_create_memory();
    CHECK(fs != 0);

    fd = ffs_open(fs, "/f", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "0123456789", 10), 10);

    CHECK_EQ(ffs_fstat(fs, fd, &st), 0);
    CHECK_EQ(st.st_size, 10);
    CHECK(FFS_S_ISREG(st.st_mode));

    /* pread/pwrite do not move the offset */
    CHECK_EQ(ffs_lseek(fs, fd, 0, SEEK_SET), 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_pread(fs, fd, buf, 3, 4), 3);
    CHECK_STR(buf, "456");
    CHECK_EQ(ffs_lseek(fs, fd, 0, SEEK_CUR), 0);
    CHECK_EQ(ffs_pwrite(fs, fd, "XY", 2, 1), 2);
    CHECK_EQ(ffs_lseek(fs, fd, 0, SEEK_CUR), 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, sizeof(buf)), 10);
    CHECK_STR(buf, "0XY3456789");

    /* dup shares the offset */
    d2 = ffs_dup(fs, fd);
    CHECK(d2 >= 0);
    CHECK_EQ(ffs_lseek(fs, fd, 3, SEEK_SET), 3);
    CHECK_EQ(ffs_lseek(fs, d2, 0, SEEK_CUR), 3);
    CHECK_EQ(ffs_close_fd(fs, d2), 0);

    /* dup2 to a specific fd */
    d2 = ffs_dup2(fs, fd, 7);
    CHECK_EQ(d2, 7);
    CHECK_EQ(ffs_lseek(fs, 7, 0, SEEK_CUR), 3);
    CHECK_EQ(ffs_close_fd(fs, 7), 0);

    /* fcntl */
    CHECK_EQ(ffs_fcntl(fs, fd, F_GETFL, 0), O_RDWR);
    CHECK_EQ(ffs_fcntl(fs, fd, F_SETFL, O_APPEND), 0);
    CHECK_EQ(ffs_fcntl(fs, fd, F_GETFL, 0), O_RDWR | O_APPEND);
    CHECK_EQ(ffs_fcntl(fs, fd, F_SETFL, 0), 0);
    d2 = ffs_fcntl(fs, fd, F_DUPFD, 4);
    CHECK(d2 >= 4);
    CHECK_EQ(ffs_close_fd(fs, d2), 0);

    /* fchmod/fchown (root) */
    CHECK_EQ(ffs_fchmod(fs, fd, 0600), 0);
    CHECK_EQ(ffs_fstat(fs, fd, &st), 0);
    CHECK_EQ(st.st_mode & 0777UL, 0600UL);
    CHECK_EQ(ffs_fchown(fs, fd, 1000, 1000), 0);
    CHECK_EQ(ffs_fstat(fs, fd, &st), 0);
    CHECK_EQ(st.st_uid, 1000);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    /* O_DIRECTORY: open a dir, then fchdir */
    CHECK_EQ(ffs_mkdir(fs, "/d", 0755), 0);
    fd = ffs_open(fs, "/d", O_RDONLY | O_DIRECTORY, 0);
    CHECK(fd >= 0);
    CHECK_ERRNO(ffs_read(fs, fd, buf, 1), EISDIR);
    CHECK_ERRNO(ffs_write(fs, fd, "x", 1), EISDIR);
    CHECK_EQ(ffs_fstat(fs, fd, &st), 0);
    CHECK(FFS_S_ISDIR(st.st_mode));
    CHECK_EQ(ffs_fchdir(fs, fd), 0);
    CHECK_EQ(ffs_getcwd(fs, cwd, sizeof(cwd)), 0);
    CHECK_STR(cwd, "/d");
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    /* O_DIRECTORY on a file -> ENOTDIR */
    fd = ffs_open(fs, "/f", O_RDONLY | O_DIRECTORY, 0);
    CHECK(fd == -1);
    CHECK(errno == ENOTDIR);

    /* O_NOFOLLOW rejects a final symlink */
    CHECK_EQ(ffs_symlink(fs, "/f", "/link"), 0);
    CHECK_ERRNO(ffs_open(fs, "/link", O_RDONLY | O_NOFOLLOW, 0), ELOOP);

    ffs_destroy(fs);
    TEST_DONE();
}
