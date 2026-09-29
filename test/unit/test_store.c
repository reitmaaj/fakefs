#include "test.h"
#include "unit.h"

#include <sqlite3.h>

static long scalar(ffs *fs, const char *sql)
{
    sqlite3_stmt *s;
    long v;

    s = NULL;
    v = -1;
    if (sqlite3_prepare_v2(fs->db, sql, -1, &s, NULL) != SQLITE_OK)
    {
        return -1;
    }
    if (sqlite3_step(s) == SQLITE_ROW)
    {
        v = (long)sqlite3_column_int64(s, 0);
    }
    sqlite3_finalize(s);
    return v;
}

static long inode_count(ffs *fs, long ino)
{
    sqlite3_stmt *s;
    long v;

    s = NULL;
    v = -1;
    if (sqlite3_prepare_v2(fs->db, "SELECT COUNT(*) FROM inodes WHERE ino = ?",
                           -1, &s, NULL) != SQLITE_OK)
    {
        return -1;
    }
    sqlite3_bind_int64(s, 1, ino);
    if (sqlite3_step(s) == SQLITE_ROW)
    {
        v = (long)sqlite3_column_int64(s, 0);
    }
    sqlite3_finalize(s);
    return v;
}

static long inode_nlink(ffs *fs, long ino)
{
    sqlite3_stmt *s;
    long v;

    s = NULL;
    v = -1;
    if (sqlite3_prepare_v2(fs->db, "SELECT nlink FROM inodes WHERE ino = ?",
                           -1, &s, NULL) != SQLITE_OK)
    {
        return -1;
    }
    sqlite3_bind_int64(s, 1, ino);
    if (sqlite3_step(s) == SQLITE_ROW)
    {
        v = (long)sqlite3_column_int64(s, 0);
    }
    sqlite3_finalize(s);
    return v;
}

static void check_dirents(ffs *fs)
{
    CHECK_EQ(scalar(fs, "SELECT COUNT(*) FROM dirents d"
                        " LEFT JOIN inodes i ON i.ino = d.ino"
                        " WHERE i.ino IS NULL"),
             0);
}

static void check_parents(ffs *fs)
{
    CHECK_EQ(scalar(fs, "SELECT COUNT(*) FROM inodes i"
                        " LEFT JOIN inodes p ON p.ino = i.parent"
                        " WHERE p.ino IS NULL"),
             0);
    CHECK_EQ(scalar(fs, "SELECT COUNT(*) FROM inodes i"
                        " JOIN inodes p ON p.ino = i.parent"
                        " WHERE (p.mode & 16384) = 0"),
             0);
}

static void check_extents(ffs *fs)
{
    CHECK_EQ(scalar(fs, "SELECT COUNT(*) FROM file_extents"
                        " WHERE len < 1 OR len > 65536"),
             0);
    CHECK_EQ(scalar(fs, "SELECT COUNT(*) FROM file_extents a"
                        " JOIN file_extents b ON a.ino = b.ino"
                        " AND a.off < b.off AND a.off + a.len > b.off"),
             0);
    CHECK_EQ(scalar(fs, "SELECT COUNT(*) FROM file_extents e"
                        " JOIN inodes i ON i.ino = e.ino"
                        " WHERE e.off + e.len > i.size"),
             0);
}

static void check_root(ffs *fs)
{
    CHECK_EQ(scalar(fs, "SELECT COUNT(*) FROM inodes WHERE ino = 1"), 1);
    CHECK_EQ(scalar(fs, "SELECT parent FROM inodes WHERE ino = 1"), 1);
    CHECK_EQ(scalar(fs, "SELECT nlink FROM inodes WHERE ino = 1"), 1);
    CHECK_EQ(scalar(fs, "SELECT size FROM inodes WHERE ino = 1"), 0);
    CHECK_EQ(scalar(fs, "SELECT COUNT(*) FROM inodes"
                        " WHERE ino = 1 AND (mode & 16384) != 0"),
             1);
    CHECK_EQ(scalar(fs, "SELECT COUNT(*) FROM dirents"), 0);
    CHECK_EQ(scalar(fs, "SELECT COUNT(*) FROM file_extents"), 0);
}

static void root_checks(void)
{
    ffs *fs;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    check_root(fs);
    ffs_destroy(fs);
}

static void namespace_checks(void)
{
    ffs *fs;
    int fd;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    CHECK_EQ(ffs_mkdir(fs, "/a", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/a/b", 0755), 0);
    fd = ffs_open(fs, "/a/b/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "data", 4), 4);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_link(fs, "/a/b/f", "/a/b/g"), 0);
    CHECK_EQ(ffs_symlink(fs, "f", "/a/b/l"), 0);
    check_dirents(fs);
    check_parents(fs);
    check_extents(fs);
    ffs_destroy(fs);
}

static void dir_nlink_checks(void)
{
    ffs *fs;
    long n;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    n = scalar(fs, "SELECT nlink FROM inodes WHERE ino = 1");
    CHECK_EQ(ffs_mkdir(fs, "/d", 0755), 0);
    CHECK_EQ(scalar(fs, "SELECT nlink FROM inodes WHERE ino = 1"), n + 1);
    CHECK_EQ(ffs_rmdir(fs, "/d"), 0);
    CHECK_EQ(scalar(fs, "SELECT nlink FROM inodes WHERE ino = 1"), n);
    ffs_destroy(fs);
}

static void file_nlink_checks(void)
{
    ffs *fs;
    long ino;
    int fd;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    fd = ffs_open(fs, "/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    ino = scalar(fs, "SELECT ino FROM dirents WHERE name = 'f'");
    CHECK_EQ(inode_nlink(fs, ino), 1);
    CHECK_EQ(ffs_link(fs, "/f", "/g"), 0);
    CHECK_EQ(inode_nlink(fs, ino), 2);
    CHECK_EQ(ffs_unlink(fs, "/f"), 0);
    CHECK_EQ(inode_nlink(fs, ino), 1);
    CHECK_EQ(ffs_unlink(fs, "/g"), 0);
    CHECK_EQ(inode_count(fs, ino), 0);
    check_dirents(fs);
    ffs_destroy(fs);
}

static void extent_checks(void)
{
    ffs *fs;
    int fd;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    fd = ffs_open(fs, "/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "abcdef", 6), 6);
    CHECK_EQ(ffs_lseek(fs, fd, 100000, SEEK_SET), 100000);
    CHECK_EQ(ffs_write(fs, fd, "xyz", 3), 3);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(scalar(fs, "SELECT COUNT(*) FROM file_extents"), 2);
    CHECK_EQ(scalar(fs, "SELECT size FROM inodes WHERE ino ="
                        " (SELECT ino FROM dirents WHERE name = 'f')"),
             100003);
    CHECK_EQ(scalar(fs, "SELECT MAX(off + len) FROM file_extents WHERE ino ="
                        " (SELECT ino FROM dirents WHERE name = 'f')"),
             100003);
    check_extents(fs);
    ffs_destroy(fs);
}

static void zero_checks(void)
{
    ffs *fs;
    char zeros[64];
    int fd;

    memset(zeros, 0, sizeof(zeros));
    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    fd = ffs_open(fs, "/z", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, zeros, sizeof(zeros)), (long)sizeof(zeros));
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(scalar(fs, "SELECT COUNT(*) FROM file_extents"), 0);
    CHECK_EQ(scalar(fs, "SELECT size FROM inodes WHERE ino ="
                        " (SELECT ino FROM dirents WHERE name = 'z')"),
             (long)sizeof(zeros));
    ffs_destroy(fs);
}

static void truncate_checks(void)
{
    ffs *fs;
    int fd;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    fd = ffs_open(fs, "/t", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "abcdefghij", 10), 10);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_truncate(fs, "/t", 4), 0);
    CHECK_EQ(scalar(fs, "SELECT size FROM inodes WHERE ino ="
                        " (SELECT ino FROM dirents WHERE name = 't')"),
             4);
    CHECK_EQ(scalar(fs, "SELECT COALESCE(MAX(off + len), 0) FROM file_extents"
                        " WHERE ino = (SELECT ino FROM dirents WHERE name = 't')"),
             4);
    check_extents(fs);
    ffs_destroy(fs);
}

static void unlink_open_checks(void)
{
    ffs *fs;
    long ino;
    int fd;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    fd = ffs_open(fs, "/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    ino = scalar(fs, "SELECT ino FROM dirents WHERE name = 'f'");
    CHECK_EQ(ffs_unlink(fs, "/f"), 0);
    CHECK_EQ(scalar(fs, "SELECT COUNT(*) FROM dirents WHERE name = 'f'"), 0);
    CHECK_EQ(inode_count(fs, ino), 1);
    CHECK_EQ(inode_nlink(fs, ino), 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(inode_count(fs, ino), 0);
    check_dirents(fs);
    ffs_destroy(fs);
}

static void rename_checks(void)
{
    ffs *fs;
    long ino;
    int fd;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    CHECK_EQ(ffs_mkdir(fs, "/a", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/b", 0755), 0);
    fd = ffs_open(fs, "/a/f", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    ino = scalar(fs, "SELECT ino FROM dirents WHERE name = 'f'");
    CHECK_EQ(ffs_rename(fs, "/a/f", "/b/g"), 0);
    CHECK_EQ(scalar(fs, "SELECT COUNT(*) FROM dirents WHERE name = 'f'"), 0);
    CHECK_EQ(scalar(fs, "SELECT ino FROM dirents WHERE name = 'g'"), ino);
    CHECK_EQ(scalar(fs, "SELECT parent FROM inodes WHERE ino ="
                        " (SELECT ino FROM dirents WHERE name = 'g')"),
             scalar(fs, "SELECT ino FROM dirents WHERE name = 'b'"));
    check_dirents(fs);
    check_parents(fs);
    ffs_destroy(fs);
}

static void replace_checks(void)
{
    ffs *fs;
    long old_ino;
    int fd;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    fd = ffs_open(fs, "/a", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    fd = ffs_open(fs, "/b", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    old_ino = scalar(fs, "SELECT ino FROM dirents WHERE name = 'b'");
    CHECK_EQ(ffs_rename(fs, "/a", "/b"), 0);
    CHECK_EQ(inode_count(fs, old_ino), 0);
    check_dirents(fs);
    check_parents(fs);
    ffs_destroy(fs);
}

static void schema_checks(void)
{
    ffs *fs;
    int rc;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);
    rc = sqlite3_exec(fs->db, "INSERT INTO file_extents (ino,off,len,data)"
                              " VALUES (1,0,0,x'00')",
                      NULL, NULL, NULL);
    CHECK_NE(rc, SQLITE_OK);
    rc = sqlite3_exec(fs->db, "INSERT INTO file_extents (ino,off,len,data)"
                              " VALUES (1,0,65537,x'00')",
                      NULL, NULL, NULL);
    CHECK_NE(rc, SQLITE_OK);
    CHECK_EQ(scalar(fs, "SELECT COUNT(*) FROM file_extents"), 0);
    ffs_destroy(fs);
}

int main(void)
{
    root_checks();
    namespace_checks();
    dir_nlink_checks();
    file_nlink_checks();
    extent_checks();
    zero_checks();
    truncate_checks();
    unlink_open_checks();
    rename_checks();
    replace_checks();
    schema_checks();
    TEST_DONE();
}
