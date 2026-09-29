#include "test.h"
#include "fakefs.h"

static void run_script(ffs *fs)
{
    int fd;
    struct ffs_stat st;

    CHECK_EQ(ffs_mkdir(fs, "/proj", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/proj/src", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/proj/doc", 0755), 0);

    fd = ffs_open(fs, "/proj/src/main.c", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "int main(void){return 0;}\n", 25), 25);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    fd = ffs_open(fs, "/proj/doc/readme.txt", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "hello fakefs\n", 13), 13);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_link(fs, "/proj/src/main.c", "/proj/src/main.bak"), 0);
    CHECK_EQ(ffs_symlink(fs, "src/main.c", "/proj/entry"), 0);
    CHECK_EQ(ffs_rename(fs, "/proj/doc/readme.txt", "/proj/doc/README.txt"), 0);
    CHECK_EQ(ffs_chmod(fs, "/proj/src/main.c", 0600), 0);
    CHECK_EQ(ffs_chown(fs, "/proj/src/main.bak", 1000, 2000), 0);

    fd = ffs_open(fs, "/proj/src/main.c", O_RDWR, 0);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_lseek(fs, fd, 0, SEEK_END), 25);
    CHECK_EQ(ffs_write(fs, fd, "/*x*/\n", 6), 6);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_stat(fs, "/proj/entry", &st), 0);
    CHECK(FFS_S_ISREG(st.st_mode));
}

static void compare(ffs *a, ffs *b)
{
    struct ffs_statfs sa;
    struct ffs_statfs sb;
    struct ffs_stat sta;
    struct ffs_stat stb;

    CHECK_EQ(ffs_statfs(a, &sa), 0);
    CHECK_EQ(ffs_statfs(b, &sb), 0);
    CHECK_EQ(sa.f_files, sb.f_files);
    CHECK_EQ(sa.f_blocks, sb.f_blocks);

    CHECK_EQ(ffs_stat(a, "/proj/src/main.c", &sta), 0);
    CHECK_EQ(ffs_stat(b, "/proj/src/main.c", &stb), 0);
    CHECK_EQ(sta.st_size, stb.st_size);
    CHECK_EQ(sta.st_mode, stb.st_mode);
    CHECK_EQ(sta.st_nlink, stb.st_nlink);
}

int main(void)
{
    ffs *mem;
    ffs *disk;
    const char *path = "build/parity_test.db";

    remove(path);
    mem = ffs_create_memory();
    disk = ffs_create(path);
    CHECK_NOTNULL(mem);
    CHECK_NOTNULL(disk);

    run_script(mem);
    run_script(disk);
    compare(mem, disk);

    ffs_destroy(mem);
    ffs_destroy(disk);
    remove(path);

    TEST_DONE();
}
