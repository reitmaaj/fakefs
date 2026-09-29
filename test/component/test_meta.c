#include "test.h"
#include "fakefs.h"

int main(void)
{
    ffs *fs;
    struct ffs_stat st;
    struct timeval tv[2];
    char buf[8];
    int fd;

    fs = ffs_create_memory();
    CHECK(fs != 0);

    fd = ffs_open(fs, "/m", O_CREAT | O_WRONLY, 0640);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_stat(fs, "/m", &st), 0);
    CHECK_EQ(st.st_mode & 0777UL, 0640UL);
    CHECK_EQ(st.st_uid, 0);
    CHECK_EQ(st.st_gid, 0);

    CHECK_EQ(ffs_chmod(fs, "/m", 0600), 0);
    CHECK_EQ(ffs_stat(fs, "/m", &st), 0);
    CHECK_EQ(st.st_mode & 0777UL, 0600UL);

    CHECK_EQ(ffs_chown(fs, "/m", 1000, 1000), 0);
    CHECK_EQ(ffs_stat(fs, "/m", &st), 0);
    CHECK_EQ(st.st_uid, 1000);
    CHECK_EQ(st.st_gid, 1000);

    tv[0].tv_sec = 111;
    tv[0].tv_usec = 0;
    tv[1].tv_sec = 222;
    tv[1].tv_usec = 0;
    CHECK_EQ(ffs_utimes(fs, "/m", tv), 0);
    CHECK_EQ(ffs_stat(fs, "/m", &st), 0);
    CHECK_EQ((long)st.st_atime, 111);
    CHECK_EQ((long)st.st_mtime, 222);

    CHECK_EQ(ffs_mkdir(fs, "/dir", 0750), 0);
    CHECK_EQ(ffs_stat(fs, "/dir", &st), 0);
    CHECK(FFS_S_ISDIR(st.st_mode));
    CHECK_EQ(st.st_mode & 0777UL, 0750UL);
    CHECK_EQ(st.st_nlink, 2);

    CHECK_EQ(ffs_mkdir(fs, "/dir/sub", 0750), 0);
    CHECK_EQ(ffs_stat(fs, "/dir", &st), 0);
    CHECK_EQ(st.st_nlink, 3);
    CHECK_EQ(ffs_stat(fs, "/dir/sub", &st), 0);
    CHECK_EQ(st.st_nlink, 2);
    CHECK_EQ(ffs_rmdir(fs, "/dir/sub"), 0);
    CHECK_EQ(ffs_stat(fs, "/dir", &st), 0);
    CHECK_EQ(st.st_nlink, 2);

    CHECK_EQ(ffs_mkdir(fs, "/dir/sub", 0750), 0);
    CHECK_EQ(ffs_mkdir(fs, "/dir2", 0750), 0);
    CHECK_EQ(ffs_rename(fs, "/dir/sub", "/dir2/moved"), 0);
    CHECK_EQ(ffs_stat(fs, "/dir", &st), 0);
    CHECK_EQ(st.st_nlink, 2);
    CHECK_EQ(ffs_stat(fs, "/dir2", &st), 0);
    CHECK_EQ(st.st_nlink, 3);
    CHECK_EQ(ffs_stat(fs, "/dir2/moved", &st), 0);
    CHECK_EQ(st.st_nlink, 2);
    CHECK_EQ(ffs_rmdir(fs, "/dir2/moved"), 0);
    CHECK_EQ(ffs_rmdir(fs, "/dir2"), 0);

    fd = ffs_open(fs, "/at", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "x", 1), 1);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    tv[0].tv_sec = 0;
    tv[0].tv_usec = 0;
    tv[1].tv_sec = 0;
    tv[1].tv_usec = 0;
    CHECK_EQ(ffs_utimes(fs, "/at", tv), 0);
    fd = ffs_open(fs, "/at", O_RDONLY, 0);
    CHECK_EQ(ffs_read(fs, fd, buf, 1), 1);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_stat(fs, "/at", &st), 0);
    CHECK_EQ((long)st.st_atime, 0);

    CHECK_EQ(ffs_setatime(fs, 1), 0);
    fd = ffs_open(fs, "/at", O_RDONLY, 0);
    CHECK_EQ(ffs_read(fs, fd, buf, 1), 1);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_stat(fs, "/at", &st), 0);
    CHECK((long)st.st_atime > 0);

    CHECK_EQ(ffs_rmdir(fs, "/dir"), 0);
    CHECK_EQ(ffs_stat(fs, "/dir", &st), -1);
    CHECK(errno == ENOENT);

    ffs_destroy(fs);
    TEST_DONE();
}
