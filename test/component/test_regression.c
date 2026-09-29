#include "test.h"
#include "fakefs.h"

int main(void)
{
    ffs *fs;
    struct ffs_stat st;
    struct ffs_stat lst;
    char buf[64];
    int fd;

    fs = ffs_create_memory();
    CHECK(fs != 0);

    /* 1. overwriting in the middle must not truncate the file */
    fd = ffs_open(fs, "/f", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "abcdef", 6), 6);
    CHECK_EQ(ffs_lseek(fs, fd, 2, SEEK_SET), 2);
    CHECK_EQ(ffs_write(fs, fd, "XY", 2), 2);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_stat(fs, "/f", &st), 0);
    CHECK_EQ(st.st_size, 6);
    fd = ffs_open(fs, "/f", O_RDONLY, 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, sizeof(buf)), 6);
    CHECK_STR(buf, "abXYef");
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    /* 2. open(O_CREAT) on a symlink to a directory must fail EISDIR */
    CHECK_EQ(ffs_mkdir(fs, "/d", 0755), 0);
    CHECK_EQ(ffs_symlink(fs, "/d", "/linkd"), 0);
    CHECK_ERRNO(ffs_open(fs, "/linkd", O_CREAT | O_WRONLY, 0644), EISDIR);

    /* 3. open(O_CREAT) on a symlink to a file must follow the link */
    fd = ffs_open(fs, "/real", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "orig", 4), 4);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_symlink(fs, "/real", "/slink"), 0);
    fd = ffs_open(fs, "/slink", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "ZZ", 2), 2);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_stat(fs, "/real", &st), 0);
    CHECK_EQ(st.st_size, 2);
    fd = ffs_open(fs, "/real", O_RDONLY, 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, sizeof(buf)), 2);
    CHECK_STR(buf, "ZZ");
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_lstat(fs, "/slink", &lst), 0);
    CHECK(FFS_S_ISLNK(lst.st_mode));

    /* 4. open(O_CREAT|O_EXCL) on a symlink must fail EEXIST */
    CHECK_ERRNO(ffs_open(fs, "/slink", O_CREAT | O_EXCL, 0644), EEXIST);

    /* 5. a zero-length write must not extend the file */
    fd = ffs_open(fs, "/z", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "abc", 3), 3);
    CHECK_EQ(ffs_lseek(fs, fd, 100, SEEK_SET), 100);
    CHECK_EQ(ffs_write(fs, fd, "x", 0), 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_stat(fs, "/z", &st), 0);
    CHECK_EQ(st.st_size, 3);

    /* 6. write-through: two descriptors observe each other's writes */
    {
        int w = ffs_open(fs, "/wt", O_CREAT | O_RDWR, 0644);
        int rd = ffs_open(fs, "/wt", O_RDONLY, 0);
        CHECK(w >= 0);
        CHECK(rd >= 0);
        CHECK_EQ(ffs_write(fs, w, "hello", 5), 5);
        memset(buf, 0, sizeof(buf));
        CHECK_EQ(ffs_read(fs, rd, buf, sizeof(buf)), 5);
        CHECK_STR(buf, "hello");
        CHECK_EQ(ffs_close_fd(fs, w), 0);
        CHECK_EQ(ffs_close_fd(fs, rd), 0);
    }

    /* 7. getcwd into a too-small buffer reports ERANGE */
    {
        char tiny[2];
        CHECK_EQ(ffs_chdir(fs, "/d"), 0);
        CHECK_ERRNO(ffs_getcwd(fs, tiny, sizeof(tiny)), ERANGE);
    }

    /* 8. missing intermediate directory must be ENOENT, not UB */
    {
        int fd8 = ffs_open(fs, "/base", O_CREAT | O_WRONLY, 0644);
        CHECK(fd8 >= 0);
        CHECK_EQ(ffs_close_fd(fs, fd8), 0);
        CHECK_ERRNO(ffs_mkdir(fs, "/nodir/x", 0755), ENOENT);
        CHECK_ERRNO(ffs_open(fs, "/nodir/f", O_CREAT | O_WRONLY, 0644), ENOENT);
        CHECK_ERRNO(ffs_link(fs, "/base", "/nodir/g"), ENOENT);
        CHECK_ERRNO(ffs_symlink(fs, "/base", "/nodir/s"), ENOENT);
        CHECK_ERRNO(ffs_rename(fs, "/base", "/nodir/r"), ENOENT);
    }

    /* 9. an absolute symlink target resolves from root, not the link's dir */
    {
        int fd9;
        CHECK_EQ(ffs_mkdir(fs, "/subdir", 0755), 0);
        fd9 = ffs_open(fs, "/abs_target", O_CREAT | O_WRONLY, 0644);
        CHECK(fd9 >= 0);
        CHECK_EQ(ffs_write(fs, fd9, "ABS", 3), 3);
        CHECK_EQ(ffs_close_fd(fs, fd9), 0);
        CHECK_EQ(ffs_symlink(fs, "/abs_target", "/subdir/abslink"), 0);
        CHECK_EQ(ffs_stat(fs, "/subdir/abslink", &st), 0);
        CHECK(FFS_S_ISREG(st.st_mode));
        CHECK_EQ(st.st_size, 3);
    }

    /* 10. open(O_CREAT) through a dangling symlink creates the target */
    {
        int fd10;
        CHECK_EQ(ffs_symlink(fs, "dangling_target", "/drel"), 0);
        fd10 = ffs_open(fs, "/drel", O_CREAT | O_WRONLY, 0644);
        CHECK(fd10 >= 0);
        CHECK_EQ(ffs_write(fs, fd10, "x", 1), 1);
        CHECK_EQ(ffs_close_fd(fs, fd10), 0);
        CHECK_EQ(ffs_stat(fs, "/dangling_target", &st), 0);
        CHECK(FFS_S_ISREG(st.st_mode));
        CHECK_EQ(st.st_size, 1);
        CHECK_EQ(ffs_lstat(fs, "/drel", &lst), 0);
        CHECK(FFS_S_ISLNK(lst.st_mode));

        CHECK_EQ(ffs_mkdir(fs, "/sub2", 0755), 0);
        CHECK_EQ(ffs_symlink(fs, "/sub2/abstarget", "/sub2/alink"), 0);
        fd10 = ffs_open(fs, "/sub2/alink", O_CREAT | O_WRONLY, 0644);
        CHECK(fd10 >= 0);
        CHECK_EQ(ffs_write(fs, fd10, "y", 1), 1);
        CHECK_EQ(ffs_close_fd(fs, fd10), 0);
        CHECK_EQ(ffs_stat(fs, "/sub2/abstarget", &st), 0);
        CHECK(FFS_S_ISREG(st.st_mode));
        CHECK_EQ(st.st_size, 1);

        /* 10b. symlink whose target's parent dir is missing -> ENOENT */
        CHECK_EQ(ffs_symlink(fs, "nodir2/file", "/nlink"), 0);
        CHECK_ERRNO(ffs_open(fs, "/nlink", O_CREAT | O_WRONLY, 0644), ENOENT);
    }

    ffs_destroy(fs);
    TEST_DONE();
}
