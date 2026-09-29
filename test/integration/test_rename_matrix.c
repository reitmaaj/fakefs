#include "test.h"
#include "fakefs.h"

/*
 * rename() source-type x target-existence matrix. Each case runs in a fresh
 * in-memory filesystem and asserts the resulting errno and final namespace.
 */

static void setup_file(ffs *fs, const char *p)
{
    int fd = ffs_open(fs, p, O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "x", 1), 1);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
}

int main(void)
{
    ffs *fs;
    struct ffs_stat st;

    /* file -> absent: moves */
    fs = ffs_create_memory();
    setup_file(fs, "/a");
    CHECK_EQ(ffs_rename(fs, "/a", "/b"), 0);
    CHECK_EQ(ffs_stat(fs, "/a", &st), -1);
    CHECK_EQ(ffs_stat(fs, "/b", &st), 0);
    CHECK(FFS_S_ISREG(st.st_mode));
    ffs_destroy(fs);

    /* file -> file: overwrite */
    fs = ffs_create_memory();
    setup_file(fs, "/a");
    setup_file(fs, "/b");
    CHECK_EQ(ffs_rename(fs, "/a", "/b"), 0);
    CHECK_EQ(ffs_stat(fs, "/b", &st), 0);
    CHECK_EQ(st.st_size, 1);
    ffs_destroy(fs);

    /* file -> dir: EISDIR */
    fs = ffs_create_memory();
    setup_file(fs, "/a");
    CHECK_EQ(ffs_mkdir(fs, "/d", 0755), 0);
    CHECK_ERRNO(ffs_rename(fs, "/a", "/d"), EISDIR);
    ffs_destroy(fs);

    /* empty dir -> absent: moves */
    fs = ffs_create_memory();
    CHECK_EQ(ffs_mkdir(fs, "/d", 0755), 0);
    CHECK_EQ(ffs_rename(fs, "/d", "/e"), 0);
    CHECK_EQ(ffs_stat(fs, "/d", &st), -1);
    CHECK_EQ(ffs_stat(fs, "/e", &st), 0);
    CHECK(FFS_S_ISDIR(st.st_mode));
    ffs_destroy(fs);

    /* empty dir -> file: ENOTDIR */
    fs = ffs_create_memory();
    CHECK_EQ(ffs_mkdir(fs, "/d", 0755), 0);
    setup_file(fs, "/f");
    CHECK_ERRNO(ffs_rename(fs, "/d", "/f"), ENOTDIR);
    ffs_destroy(fs);

    /* empty dir -> empty dir: replace */
    fs = ffs_create_memory();
    CHECK_EQ(ffs_mkdir(fs, "/d1", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/d2", 0755), 0);
    CHECK_EQ(ffs_rename(fs, "/d1", "/d2"), 0);
    CHECK_EQ(ffs_stat(fs, "/d1", &st), -1);
    CHECK_EQ(ffs_stat(fs, "/d2", &st), 0);
    ffs_destroy(fs);

    /* empty dir -> nonempty dir: ENOTEMPTY */
    fs = ffs_create_memory();
    CHECK_EQ(ffs_mkdir(fs, "/d1", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/d2", 0755), 0);
    setup_file(fs, "/d2/child");
    CHECK_ERRNO(ffs_rename(fs, "/d1", "/d2"), ENOTEMPTY);
    ffs_destroy(fs);

    /* nonempty dir -> absent: moves subtree */
    fs = ffs_create_memory();
    CHECK_EQ(ffs_mkdir(fs, "/d", 0755), 0);
    setup_file(fs, "/d/child");
    CHECK_EQ(ffs_rename(fs, "/d", "/e"), 0);
    CHECK_EQ(ffs_stat(fs, "/e/child", &st), 0);
    ffs_destroy(fs);

    /* dir -> own subtree: EINVAL */
    fs = ffs_create_memory();
    CHECK_EQ(ffs_mkdir(fs, "/d", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/d/sub", 0755), 0);
    CHECK_ERRNO(ffs_rename(fs, "/d", "/d/sub"), EINVAL);
    CHECK_ERRNO(ffs_rename(fs, "/d", "/d/sub/deep"), EINVAL);
    ffs_destroy(fs);

    /* symlink -> absent: renames the link itself */
    fs = ffs_create_memory();
    setup_file(fs, "/target");
    CHECK_EQ(ffs_symlink(fs, "target", "/link"), 0);
    CHECK_EQ(ffs_rename(fs, "/link", "/link2"), 0);
    CHECK_EQ(ffs_lstat(fs, "/link2", &st), 0);
    CHECK(FFS_S_ISLNK(st.st_mode));
    CHECK_EQ(ffs_stat(fs, "/target", &st), 0);
    ffs_destroy(fs);

    TEST_DONE();
}
