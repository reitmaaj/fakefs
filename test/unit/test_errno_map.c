#include "test.h"
#include "unit.h"

int main(void)
{
    ffs *fs;
    sqlite3_stmt *s;
    struct finode fi;
    long mode;
    long parent;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);

    CHECK_EQ(dirent_insert(fs, 1, "a", 1), 0);
    CHECK_EQ(dirent_insert(fs, 1, "a", 1), -EEXIST);
    CHECK_EQ(dirent_lookup(fs, 1, "missing"), -ENOENT);

    CHECK_NULL(prepare(fs, "THIS IS NOT VALID SQL"));

    CHECK_EQ(stat_count(fs, "THIS IS NOT VALID SQL"), -EIO);

    memset(&fi, 0, sizeof(fi));
    CHECK_EQ(inode_load(fs, 99999, &fi), -ENOENT);

    CHECK_EQ(inode_mode(fs, 99999, &mode), -ENOENT);

    CHECK_EQ(inode_parent(fs, 99999, &parent), -ENOENT);

    CHECK_EQ(truncate_ino(fs, 1, 0), -EISDIR);

    s = prepare(fs, "SELECT COUNT(*) FROM inodes");
    CHECK_NOTNULL(s);
    sqlite3_finalize(s);

    memset(&fi, 0, sizeof(fi));
    CHECK(inode_new(fs, &fi) > 0);

    ffs_destroy(fs);
    TEST_DONE();
}
