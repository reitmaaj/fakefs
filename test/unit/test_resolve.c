#include "test.h"
#include "unit.h"

int main(void)
{
    ffs *fs;
    struct ffs_stat st;
    char name[FFS_NAME_MAX + 1];
    long parent;
    long ino;
    long start;
    long ino_a;
    long ino_b;
    long ino_f;
    long ino_link;
    int fd;
    int d;
    int f;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);

    CHECK_EQ(ffs_mkdir(fs, "/a", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/a/b", 0755), 0);
    fd = ffs_open(fs, "/a/b/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_stat(fs, "/a", &st), 0);
    ino_a = (long)st.st_ino;
    CHECK_EQ(ffs_stat(fs, "/a/b", &st), 0);
    ino_b = (long)st.st_ino;
    CHECK_EQ(ffs_stat(fs, "/a/b/f", &st), 0);
    ino_f = (long)st.st_ino;

    name[0] = '\0';
    CHECK_EQ(resolve(fs, "/a/b/f", 1, &parent, name, &ino), 0);
    CHECK_EQ(ino, ino_f);
    CHECK_EQ(parent, ino_b);
    CHECK_EQ(name[0], '\0');

    CHECK_EQ(resolve(fs, "/a/b/f", 0, &parent, name, &ino), 0);
    CHECK_EQ(ino, ino_f);
    CHECK_STR(name, "f");

    CHECK_EQ(resolve(fs, "/a/b", 1, &parent, name, &ino), 0);
    CHECK_EQ(ino, ino_b);

    CHECK_EQ(ffs_chdir(fs, "/a/b"), 0);
    CHECK_EQ(resolve(fs, "f", 1, &parent, name, &ino), 0);
    CHECK_EQ(ino, ino_f);

    CHECK_EQ(resolve(fs, ".", 1, &parent, name, &ino), 0);
    CHECK_EQ(ino, ino_b);
    CHECK_EQ(resolve(fs, "..", 1, &parent, name, &ino), 0);
    CHECK_EQ(ino, ino_a);
    CHECK_EQ(resolve(fs, "..", 0, &parent, name, &ino), 0);
    CHECK_EQ(ino, ino_a);

    CHECK_EQ(resolve(fs, "f/", 1, &parent, name, &ino), -ENOTDIR);
    CHECK_EQ(resolve(fs, "/a/b/nope", 1, &parent, name, &ino), -ENOENT);
    CHECK_EQ(resolve(fs, "/a/b/f/extra", 1, &parent, name, &ino), -ENOTDIR);

    CHECK_EQ(ffs_symlink(fs, "f", "/a/b/link"), 0);
    CHECK_EQ(ffs_lstat(fs, "/a/b/link", &st), 0);
    ino_link = (long)st.st_ino;

    CHECK_EQ(resolve(fs, "/a/b/link", 0, &parent, name, &ino), 0);
    CHECK_EQ(ino, ino_link);
    CHECK_STR(name, "link");
    CHECK_EQ(resolve(fs, "/a/b/link", 1, &parent, name, &ino), 0);
    CHECK_EQ(ino, ino_f);
    CHECK_EQ(name[0], '\0');

    CHECK_EQ(ffs_symlink(fs, "/a/loop", "/a/loop"), 0);
    CHECK_EQ(resolve(fs, "/a/loop", 1, &parent, name, &ino), -ELOOP);
    CHECK_EQ(resolve(fs, "/a/loop", 0, &parent, name, &ino), 0);

    d = ffs_open(fs, "/a/b", O_RDONLY | O_DIRECTORY, 0);
    CHECK(d >= 0);
    CHECK_EQ(fd_to_inode(fs, d, &start), 0);
    CHECK_EQ(start, ino_b);
    CHECK_EQ(resolve_at(fs, d, "f", 1, 0, &parent, name, &ino), 0);
    CHECK_EQ(ino, ino_f);
    CHECK_EQ(resolve_at(fs, d, "/a/b/f", 1, 0, &parent, name, &ino), 0);
    CHECK_EQ(ino, ino_f);

    CHECK_EQ(fd_to_inode(fs, AT_FDCWD, &start), 0);
    CHECK_EQ(start, ino_b);

    f = ffs_open(fs, "/a/b/f", O_RDONLY, 0);
    CHECK(f >= 0);
    CHECK_EQ(fd_to_inode(fs, f, &start), -ENOTDIR);
    CHECK_EQ(fd_to_inode(fs, 999, &start), -EBADF);
    CHECK_EQ(ffs_close_fd(fs, f), 0);
    CHECK_EQ(ffs_close_fd(fs, d), 0);

    /* pure component predicates */
    CHECK_EQ(comp_is_dot("."), 1);
    CHECK_EQ(comp_is_dot(".."), 0);
    CHECK_EQ(comp_is_dot(".x"), 0);
    CHECK_EQ(comp_is_dotdot(".."), 1);
    CHECK_EQ(comp_is_dotdot("."), 0);
    CHECK_EQ(comp_is_dotdot("..."), 0);
    CHECK_EQ(follow_of(1, 0, 0), 1);
    CHECK_EQ(follow_of(0, 1, 1), 1);
    CHECK_EQ(follow_of(0, 1, 0), 0);
    CHECK_EQ(follow_of(0, 0, 1), 0);
    CHECK_EQ(symlink_mode(FFS_S_IFLNK), 1);
    CHECK_EQ(symlink_mode(FFS_S_IFREG), 0);
    CHECK_EQ(symlink_mode(FFS_S_IFDIR), 0);

    ffs_destroy(fs);
    TEST_DONE();
}
