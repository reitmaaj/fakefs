#include "test.h"
#include "unit.h"

static void check_perm_table(const struct ffs *fs, long mode, long uid,
                             long gid, int r, int w, int x)
{
    CHECK_EQ(perm_ok(fs, mode, uid, gid, FFS_PERM_READ), r);
    CHECK_EQ(perm_ok(fs, mode, uid, gid, FFS_PERM_WRITE), w);
    CHECK_EQ(perm_ok(fs, mode, uid, gid, FFS_PERM_EXEC), x);
}

static void helper_checks(void)
{
    struct ffs fs;

    memset(&fs, 0, sizeof(fs));
    fs.uid = 1000;
    fs.gid = 2000;
    CHECK_EQ(shift_for(&fs, 1000, 9999), FFS_SHIFT_OWNER);
    CHECK_EQ(shift_for(&fs, 9999, 2000), FFS_SHIFT_GROUP);
    CHECK_EQ(shift_for(&fs, 9999, 9999), FFS_SHIFT_OTHER);
    CHECK_EQ(group_shift(&fs, 2000), FFS_SHIFT_GROUP);
    CHECK_EQ(group_shift(&fs, 9999), FFS_SHIFT_OTHER);
    CHECK_EQ(meta_code(0), 0);
    CHECK_EQ(meta_code(-ENOENT), -EACCES);
    CHECK_EQ(access_err(1), 0);
    CHECK_EQ(access_err(0), -EACCES);
    CHECK_EQ(access_err(-EACCES), -EACCES);
    CHECK_EQ(perm_denied(1), 0);
    CHECK_EQ(perm_denied(0), -EPERM);
    CHECK_EQ(choose(1, 7, 9), 7);
    CHECK_EQ(choose(0, 7, 9), 9);
    CHECK_EQ(chain(-EACCES, 7), -EACCES);
    CHECK_EQ(chain(0, 7), 7);
}

int main(void)
{
    struct ffs fs;

    helper_checks();
    memset(&fs, 0, sizeof(fs));

    /* owner, group, other selection by uid/gid */
    fs.uid = 1000;
    fs.gid = 2000;
    check_perm_table(&fs, 0700, 1000, 2000, 1, 1, 1);
    check_perm_table(&fs, 0700, 1000, 9999, 1, 1,
                     1); /* owner wins over group */
    check_perm_table(&fs, 0700, 9999, 2000, 0, 0,
                     0); /* not owner, group has no bits */
    check_perm_table(&fs, 0070, 9999, 2000, 1, 1, 1); /* group rwx */
    check_perm_table(&fs, 0020, 9999, 2000, 0, 1, 0); /* group write only */
    check_perm_table(&fs, 0070, 9999, 9999, 0, 0, 0); /* other */
    check_perm_table(&fs, 0007, 9999, 9999, 1, 1, 1); /* other rwx */
    check_perm_table(&fs, 0001, 9999, 9999, 0, 0, 1); /* other exec */
    check_perm_table(&fs, 0004, 9999, 9999, 1, 0, 0); /* other read */
    check_perm_table(&fs, 0754, 9999, 9999, 1, 0, 0); /* other read only of 4 */
    check_perm_table(&fs, 0000, 9999, 9999, 0, 0, 0);

    /* root bypasses read/write but needs an exec bit for exec */
    fs.uid = 0;
    fs.gid = 0;
    CHECK_EQ(fs_is_root(&fs), 1);
    check_perm_table(&fs, 0000, 1000, 2000, 1, 1, 0);
    check_perm_table(&fs, 0100, 1000, 2000, 1, 1, 1);
    check_perm_table(&fs, 0010, 1000, 2000, 1, 1, 1);
    check_perm_table(&fs, 0001, 1000, 2000, 1, 1, 1);

    fs.uid = 1000;
    CHECK_EQ(fs_is_root(&fs), 0);

    /* root_ok */
    CHECK_EQ(root_ok(0000, FFS_PERM_EXEC), 0);
    CHECK_EQ(root_ok(0111, FFS_PERM_EXEC), 1);
    CHECK_EQ(root_ok(0001, FFS_PERM_EXEC), 1);
    CHECK_EQ(root_ok(0000, FFS_PERM_READ), 1);
    CHECK_EQ(root_ok(0000, FFS_PERM_WRITE), 1);

    /* need_read / need_write */
    CHECK_EQ(need_read(O_RDONLY), 1);
    CHECK_EQ(need_read(O_RDWR), 1);
    CHECK_EQ(need_read(O_WRONLY), 0);
    CHECK_EQ(need_write(O_WRONLY), 1);
    CHECK_EQ(need_write(O_RDWR), 1);
    CHECK_EQ(need_write(O_RDONLY), 0);

    TEST_DONE();
}
