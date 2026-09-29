#include "test.h"
#include "fakefs.h"

int main(void)
{
    ffs *fs;
    ffs_dir *d;
    char name[64];
    char cwd[256];
    int r;

    fs = ffs_create_memory();
    CHECK(fs != 0);

    CHECK_EQ(ffs_mkdir(fs, "/a", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/a/b", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/a/b/c", 0755), 0);

    CHECK_EQ(ffs_chdir(fs, "/a/b"), 0);
    CHECK_EQ(ffs_getcwd(fs, cwd, sizeof(cwd)), 0);
    CHECK_STR(cwd, "/a/b");

    CHECK_EQ(ffs_chdir(fs, ".."), 0);
    CHECK_EQ(ffs_getcwd(fs, cwd, sizeof(cwd)), 0);
    CHECK_STR(cwd, "/a");

    CHECK_EQ(ffs_chdir(fs, "b/c"), 0);
    CHECK_EQ(ffs_getcwd(fs, cwd, sizeof(cwd)), 0);
    CHECK_STR(cwd, "/a/b/c");

    CHECK_EQ(ffs_chdir(fs, "/"), 0);
    CHECK_EQ(ffs_getcwd(fs, cwd, sizeof(cwd)), 0);
    CHECK_STR(cwd, "/");

    CHECK_EQ(ffs_chdir(fs, "/a"), 0);
    CHECK_EQ(ffs_mkdir(fs, "x", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "y", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "z", 0755), 0);

    d = ffs_opendir(fs, ".");
    CHECK(d != 0);
    r = ffs_readdir(d, name, sizeof(name));
    CHECK_EQ(r, 1);
    CHECK_STR(name, ".");
    r = ffs_readdir(d, name, sizeof(name));
    CHECK_EQ(r, 1);
    CHECK_STR(name, "..");
    r = ffs_readdir(d, name, sizeof(name));
    CHECK_EQ(r, 1);
    CHECK_STR(name, "b");
    r = ffs_readdir(d, name, sizeof(name));
    CHECK_EQ(r, 1);
    CHECK_STR(name, "x");
    r = ffs_readdir(d, name, sizeof(name));
    CHECK_EQ(r, 1);
    CHECK_STR(name, "y");
    r = ffs_readdir(d, name, sizeof(name));
    CHECK_EQ(r, 1);
    CHECK_STR(name, "z");
    CHECK_EQ(ffs_readdir(d, name, sizeof(name)), 0);
    CHECK_EQ(ffs_closedir(d), 0);

    CHECK_EQ(ffs_rmdir(fs, "/a/x"), 0);
    CHECK_EQ(ffs_rmdir(fs, "/a/y"), 0);
    CHECK_EQ(ffs_rmdir(fs, "/a/z"), 0);

    CHECK_ERRNO(ffs_rmdir(fs, "/a/b"), ENOTEMPTY);
    CHECK_ERRNO(ffs_rmdir(fs, "/a"), ENOTEMPTY);

    errno = 0;
    d = ffs_opendir(fs, "/a/nope");
    CHECK(d == 0);
    CHECK(errno == ENOENT);

    ffs_destroy(fs);
    TEST_DONE();
}
