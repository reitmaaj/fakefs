#include "test.h"
#include "unit.h"

static int op_insert(ffs *fs, void *ctx)
{
    struct finode fi;
    long ino;
    int r;

    (void)ctx;
    fi.parent = FFS_ROOT_INO;
    fi.mode = 0100644L;
    fi.uid = 0;
    fi.gid = 0;
    fi.nlink = 1;
    fi.size = 0;
    fi.atime = 1;
    fi.mtime = 1;
    fi.ctime = 1;
    fi.target = 0;
    ino = inode_new(fs, &fi);
    if (ino < 0)
    {
        return (int)ino;
    }
    r = dirent_insert(fs, FFS_ROOT_INO, "x", ino);
    return r;
}

static int op_insert_ok(ffs *fs, void *ctx)
{
    int r;

    r = op_insert(fs, ctx);
    return (r < 0) ? r : 0;
}

static int op_insert_fail(ffs *fs, void *ctx)
{
    int r;

    r = op_insert(fs, ctx);
    return (r < 0) ? r : -EINVAL;
}

static void range_checks(void)
{
    CHECK_EQ(fd_range_ok(0), 1);
    CHECK_EQ(fd_range_ok(FFS_MAX_FD - 1), 1);
    CHECK_EQ(fd_range_ok(FFS_MAX_FD), 0);
    CHECK_EQ(fd_range_ok(-1), 0);
}

static void result_checks(void)
{
    CHECK_EQ(done_result(SQLITE_DONE), 0);
    CHECK_EQ(done_result(SQLITE_ROW), -EIO);
}

static void code_checks(void)
{
    CHECK_EQ(either_null(0, "x"), 1);
    CHECK_EQ(either_null("x", 0), 1);
    CHECK_EQ(either_null(0, 0), 1);
    CHECK_EQ(either_null("x", "y"), 0);
    CHECK_EQ(any_null3(0, "x", "y"), 1);
    CHECK_EQ(any_null3("x", 0, "y"), 1);
    CHECK_EQ(any_null3("x", "y", 0), 1);
    CHECK_EQ(any_null3("x", "y", "z"), 0);
    CHECK_EQ(neg_code(-EIO), -EIO);
    CHECK_EQ(neg_code(0), 0);
    CHECK_EQ(neg_code(7), 0);
    errno = 0;
    CHECK_EQ(fail_code(0), 0);
    CHECK_EQ(fail_code(-ENOENT), -1);
    CHECK_EQ(errno, ENOENT);
}

static void db_checks(void)
{
    CHECK_EQ(exec_code(SQLITE_OK), 0);
    CHECK_EQ(exec_code(SQLITE_ERROR), -EIO);
    CHECK_STR(begin_sql(0), FFS_SQL_BEGIN);
    CHECK_STR(begin_sql(1), FFS_SQL_SAVEPOINT);
    CHECK_STR(commit_sql(0), FFS_SQL_COMMIT);
    CHECK_STR(commit_sql(1), FFS_SQL_RELEASE);
    CHECK_EQ(depth_down(0), 0);
    CHECK_EQ(depth_down(3), 2);
    CHECK_EQ(root_code(0), 0);
    CHECK_EQ(root_code(-EIO), -EIO);
    CHECK_EQ(root_code(2), 0);
    CHECK(choose_ptr(1, (void *)"a", (void *)"b") == (void *)"a");
    CHECK(choose_ptr(0, (void *)"a", (void *)"b") == (void *)"b");
}

int main(void)
{
    ffs *fs;
    char name[FFS_NAME_MAX + 1];
    long parent;
    long ino;
    int fd;
    int of;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);

    errno = 0;
    CHECK_EQ(util_fail(EINVAL), -1);
    CHECK_EQ(errno, EINVAL);
    errno = 0;
    CHECK_EQ(util_fail(ENOMEM), -1);
    CHECK_EQ(errno, ENOMEM);
    errno = 0;
    CHECK_EQ(util_fail_r(-ENOENT), -1);
    CHECK_EQ(errno, ENOENT);
    errno = 0;
    CHECK_EQ(util_fail_r(-EACCES), -1);
    CHECK_EQ(errno, EACCES);

    CHECK_EQ(fd_valid(fs, 0), 0);
    CHECK_EQ(fd_bad(fs, -1), 1);
    CHECK_EQ(fd_bad(0, 0), 1);
    range_checks();
    result_checks();
    code_checks();
    db_checks();
    fd = ffs_open(fs, "/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(fd_valid(fs, fd), 1);
    CHECK_EQ(fd_bad(fs, fd), 0);
    CHECK_EQ(fd_valid(fs, FFS_MAX_FD), 0);
    CHECK_EQ(fd_valid(fs, -1), 0);
    CHECK_EQ(fd_of(fs, fd, &of), 0);
    CHECK(of >= 0 && of < FFS_MAX_FD);
    CHECK_EQ(fd_of(fs, FFS_MAX_FD, &of), -EBADF);
    CHECK_EQ(fd_of(fs, -1, &of), -EBADF);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(txn_run(fs, op_insert_ok, 0), 0);
    CHECK_EQ(resolve(fs, "/x", 1, &parent, name, &ino), 0);

    CHECK_EQ(ffs_unlink(fs, "/x"), 0);
    CHECK_EQ(txn_run(fs, op_insert_fail, 0), -EINVAL);
    CHECK_EQ(resolve(fs, "/x", 1, &parent, name, &ino), -ENOENT);

    ffs_destroy(fs);
    TEST_DONE();
}
