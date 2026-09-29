#include "test.h"
#include "fakefs.h"

int main(void)
{
    ffs *fs;
    struct ffs_stat st;
    int fd;

    fs = ffs_create_memory();
    CHECK(fs != 0);

    /* root sets up /d owned by 1000:2000, mode 0755 */
    CHECK_EQ(ffs_mkdir(fs, "/d", 0755), 0);
    CHECK_EQ(ffs_chown(fs, "/d", 1000, 2000), 0);

    /* acting as 1000:2000, create /d/f (inherits identity) */
    CHECK_EQ(ffs_setids(fs, 1000, 2000), 0);
    fd = ffs_open(fs, "/d/f", O_CREAT | O_RDWR, 0600);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "x", 1), 1);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_stat(fs, "/d/f", &st), 0);
    CHECK_EQ(st.st_uid, 1000);
    CHECK_EQ(st.st_gid, 2000);
    CHECK_EQ(st.st_mode & 0777UL, 0600UL);

    /* owner can write */
    fd = ffs_open(fs, "/d/f", O_WRONLY, 0);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    /* non-owner cannot write or read (0600) */
    CHECK_EQ(ffs_setids(fs, 3000, 2000), 0);
    CHECK_ERRNO(ffs_open(fs, "/d/f", O_WRONLY, 0), EACCES);
    CHECK_ERRNO(ffs_open(fs, "/d/f", O_RDONLY, 0), EACCES);

    /* non-owner chmod/chown -> EPERM (still traversable) */
    CHECK_ERRNO(ffs_chmod(fs, "/d/f", 0644), EPERM);
    CHECK_ERRNO(ffs_chown(fs, "/d/f", 0, 0), EPERM);

    /* owner may chmod */
    CHECK_EQ(ffs_setids(fs, 1000, 2000), 0);
    CHECK_EQ(ffs_chmod(fs, "/d/f", 0644), 0);
    CHECK_EQ(ffs_stat(fs, "/d/f", &st), 0);
    CHECK_EQ(st.st_mode & 0777UL, 0644UL);

    /* search permission: /d mode 0700 blocks traversal for uid 3000 */
    CHECK_EQ(ffs_chmod(fs, "/d", 0700), 0);
    fd = ffs_open(fs, "/d/inner", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_setids(fs, 3000, 2000), 0);
    CHECK_ERRNO(ffs_stat(fs, "/d/inner", &st), EACCES);
    CHECK_ERRNO(ffs_open(fs, "/d/inner", O_RDONLY, 0), EACCES);
    CHECK_ERRNO(ffs_chdir(fs, "/d"), EACCES);

    /* root bypasses read/write */
    CHECK_EQ(ffs_setids(fs, 0, 0), 0);
    fd = ffs_open(fs, "/d/f", O_RDONLY, 0);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    /* non-writable parent blocks creation */
    CHECK_EQ(ffs_mkdir(fs, "/ro", 0555), 0);
    CHECK_EQ(ffs_mkdir(fs, "/w", 0755), 0);
    CHECK_EQ(ffs_chown(fs, "/w", 1000, 2000), 0);

    CHECK_EQ(ffs_setids(fs, 3000, 2000), 0);
    CHECK_ERRNO(ffs_open(fs, "/ro/new", O_CREAT | O_WRONLY, 0644), EACCES);
    CHECK_ERRNO(ffs_mkdir(fs, "/ro/sub", 0755), EACCES);
    CHECK_ERRNO(ffs_open(fs, "/w/new", O_CREAT | O_WRONLY, 0644), EACCES);

    /* owner (1000) can create in /w */
    CHECK_EQ(ffs_setids(fs, 1000, 2000), 0);
    fd = ffs_open(fs, "/w/new", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    /* umask applies to creation (default 022) */
    CHECK_EQ(ffs_setids(fs, 0, 0), 0);
    CHECK_EQ(ffs_setumask(fs, 022), 0);
    fd = ffs_open(fs, "/um", O_CREAT | O_WRONLY, 0666);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_stat(fs, "/um", &st), 0);
    CHECK_EQ(st.st_mode & 0777UL, 0644UL);

    ffs_destroy(fs);
    TEST_DONE();
}
