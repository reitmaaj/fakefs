#include "test.h"
#include "unit.h"

int main(void)
{
    ffs *fs;
    struct ffs_stat st;
    char buf[FFS_PATH_MAX + 1];
    long ino_f;
    long ino_d;
    int fd;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);

    CHECK_EQ(ffs_mkdir(fs, "/d", 0755), 0);
    fd = ffs_open(fs, "/d/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "abc", 3), 3);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_stat(fs, "/d/f", &st), 0);
    ino_f = (long)st.st_ino;
    CHECK_EQ(ffs_stat(fs, "/d", &st), 0);
    ino_d = (long)st.st_ino;

    CHECK_EQ(fill_stat(fs, ino_f, &st), 0);
    CHECK(FFS_S_ISREG(st.st_mode));
    CHECK_EQ(st.st_nlink, 1);
    CHECK_EQ(st.st_size, 3);
    CHECK_EQ(st.st_ino, (unsigned long)ino_f);

    CHECK_EQ(fill_stat(fs, ino_d, &st), 0);
    CHECK(FFS_S_ISDIR(st.st_mode));
    CHECK_EQ(st.st_nlink, 2);

    CHECK_EQ(build_path(fs, ino_f, buf, sizeof(buf)), 0);
    CHECK_STR(buf, "/d/f");
    CHECK_EQ(build_path(fs, ino_d, buf, sizeof(buf)), 0);
    CHECK_STR(buf, "/d");
    CHECK_EQ(build_path(fs, 1, buf, sizeof(buf)), 0);
    CHECK_STR(buf, "/");

    CHECK_EQ(build_path(fs, ino_f, buf, 3), -ERANGE);
    CHECK_EQ(build_path(fs, 99999, buf, sizeof(buf)), -ENOENT);

    CHECK_EQ(ffs_mkdir(fs, "/a", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/a/b", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/a/b/c", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/a/b/c/d", 0755), 0);
    fd = ffs_open(fs, "/a/b/c/d/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_stat(fs, "/a/b/c/d/f", &st), 0);
    CHECK_EQ(build_path(fs, (long)st.st_ino, buf, sizeof(buf)), 0);
    CHECK_STR(buf, "/a/b/c/d/f");
    CHECK_EQ(build_path(fs, 99998, buf, sizeof(buf)), -ENOENT);

    CHECK_EQ(ffs_chdir(fs, "/a/b/c"), 0);
    CHECK_EQ(ffs_getcwd(fs, buf, sizeof(buf)), 0);
    CHECK_STR(buf, "/a/b/c");
    CHECK_EQ(ffs_realpath(fs, "d/f", buf, sizeof(buf)), 0);
    CHECK_STR(buf, "/a/b/c/d/f");
    CHECK_EQ(ffs_chdir(fs, "/"), 0);

    CHECK(stat_count(fs, "SELECT COUNT(*) FROM inodes") >= 3);
    CHECK_EQ(stat_count(fs, "SELECT COUNT(*) FROM dirents WHERE parent = 1"),
             2);

    ffs_destroy(fs);
    TEST_DONE();
}
