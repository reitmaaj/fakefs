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

    fd = ffs_open(fs, "/x", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "data", 4), 4);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_stat(fs, "/x", &st), 0);
    CHECK_EQ(ffs_link(fs, "/x", "/y"), 0);

    CHECK_EQ(ffs_stat(fs, "/y", &st), 0);
    CHECK_EQ(ffs_lstat(fs, "/x", &lst), 0);
    CHECK_EQ(st.st_ino, lst.st_ino);
    CHECK_EQ(st.st_nlink, 2);

    CHECK_EQ(ffs_unlink(fs, "/x"), 0);
    CHECK_EQ(ffs_stat(fs, "/x", &st), -1);
    CHECK(errno == ENOENT);
    CHECK_EQ(ffs_stat(fs, "/y", &st), 0);
    CHECK_EQ(st.st_nlink, 1);
    CHECK_EQ(st.st_size, 4);

    CHECK_EQ(ffs_mkdir(fs, "/d", 0755), 0);
    CHECK_ERRNO(ffs_link(fs, "/d", "/d2"), EPERM);

    CHECK_EQ(ffs_symlink(fs, "/y", "/link"), 0);
    CHECK_EQ(ffs_lstat(fs, "/link", &lst), 0);
    CHECK(FFS_S_ISLNK(lst.st_mode));
    CHECK_EQ(ffs_stat(fs, "/link", &st), 0);
    CHECK(FFS_S_ISREG(st.st_mode));

    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_readlink(fs, "/link", buf, sizeof(buf)), 2);
    CHECK_STR(buf, "/y");

    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_readlink(fs, "/y", buf, sizeof(buf)), -1);
    CHECK(errno == EINVAL);

    CHECK_EQ(ffs_symlink(fs, "/link", "/link2"), 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_readlink(fs, "/link2", buf, sizeof(buf)), 5);
    CHECK_STR(buf, "/link");
    CHECK_EQ(ffs_stat(fs, "/link2", &st), 0);
    CHECK(FFS_S_ISREG(st.st_mode));

    CHECK_EQ(ffs_unlink(fs, "/link2"), 0);
    CHECK_EQ(ffs_stat(fs, "/y", &st), 0);

    /* relative symlink target resolves relative to the link's directory */
    CHECK_EQ(ffs_mkdir(fs, "/sub", 0755), 0);
    fd = ffs_open(fs, "/sub/target", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "tgt", 3), 3);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_symlink(fs, "target", "/sub/rel"), 0);
    CHECK_EQ(ffs_stat(fs, "/sub/rel", &st), 0);
    CHECK(FFS_S_ISREG(st.st_mode));
    CHECK_EQ(st.st_size, 3);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_readlink(fs, "/sub/rel", buf, sizeof(buf)), 6);
    CHECK_STR(buf, "target");

    /* hard link to a symlink inode (link() must not follow) */
    CHECK_EQ(ffs_symlink(fs, "/y", "/hsym"), 0);
    CHECK_EQ(ffs_link(fs, "/hsym", "/hlink"), 0);
    CHECK_EQ(ffs_lstat(fs, "/hlink", &lst), 0);
    CHECK(FFS_S_ISLNK(lst.st_mode));
    CHECK_EQ(ffs_lstat(fs, "/hsym", &st), 0);
    CHECK_EQ(st.st_ino, lst.st_ino);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_readlink(fs, "/hlink", buf, sizeof(buf)), 2);
    CHECK_STR(buf, "/y");

    /* two-node symlink cycle terminates with ELOOP */
    CHECK_EQ(ffs_symlink(fs, "/ca", "/cb"), 0);
    CHECK_EQ(ffs_symlink(fs, "/cb", "/ca"), 0);
    CHECK_ERRNO(ffs_stat(fs, "/ca", &st), ELOOP);

    /* lchown acts on the symlink, not the target */
    CHECK_EQ(ffs_symlink(fs, "/y", "/lch"), 0);
    CHECK_EQ(ffs_lchown(fs, "/lch", 7, 7), 0);
    CHECK_EQ(ffs_lstat(fs, "/lch", &lst), 0);
    CHECK_EQ(lst.st_uid, 7);
    CHECK_EQ(ffs_stat(fs, "/y", &st), 0);
    CHECK_EQ(st.st_uid, 0);

    ffs_destroy(fs);
    TEST_DONE();
}
