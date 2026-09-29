#include "test.h"
#include "fakefs.h"

#include <sqlite3.h>

static long scalar(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *s;
    long v;

    s = NULL;
    v = -1;
    if (sqlite3_prepare_v2(db, sql, -1, &s, NULL) != SQLITE_OK)
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

static void check_store(sqlite3 *db)
{
    CHECK_EQ(scalar(db, "SELECT COUNT(*) FROM inodes WHERE ino = 1"), 1);
    CHECK_EQ(scalar(db, "SELECT parent FROM inodes WHERE ino = 1"), 1);
    CHECK_EQ(scalar(db, "SELECT COUNT(*) FROM inodes WHERE ino = 1"
                        " AND (mode & 16384) != 0"),
             1);
    CHECK_EQ(scalar(db, "SELECT COUNT(*) FROM dirents d"
                        " LEFT JOIN inodes i ON i.ino = d.ino"
                        " WHERE i.ino IS NULL"),
             0);
    CHECK_EQ(scalar(db, "SELECT COUNT(*) FROM inodes i"
                        " LEFT JOIN inodes p ON p.ino = i.parent"
                        " WHERE p.ino IS NULL"),
             0);
    CHECK_EQ(scalar(db, "SELECT COUNT(*) FROM inodes i"
                        " JOIN inodes p ON p.ino = i.parent"
                        " WHERE (p.mode & 16384) = 0"),
             0);
    CHECK_EQ(scalar(db, "SELECT COUNT(*) FROM file_extents"
                        " WHERE len < 1 OR len > 65536"),
             0);
    CHECK_EQ(scalar(db, "SELECT COUNT(*) FROM file_extents a"
                        " JOIN file_extents b ON a.ino = b.ino"
                        " AND a.off < b.off AND a.off + a.len > b.off"),
             0);
    CHECK_EQ(scalar(db, "SELECT COUNT(*) FROM file_extents e"
                        " JOIN inodes i ON i.ino = e.ino"
                        " WHERE e.off + e.len > i.size"),
             0);
    CHECK_EQ(scalar(db, "SELECT COUNT(*) FROM inodes i"
                        " WHERE (i.mode & 16384) = 0 AND i.nlink !="
                        " (SELECT COUNT(*) FROM dirents d WHERE d.ino = i.ino)"),
             0);
    CHECK_EQ(scalar(db, "SELECT COUNT(*) FROM file_extents e WHERE e.ino ="
                        " (SELECT ino FROM dirents WHERE name = 'zero.bin')"),
             0);
}

static void run_script(ffs *fs)
{
    char zeros[32];
    int fd;

    memset(zeros, 0, sizeof(zeros));
    CHECK_EQ(ffs_mkdir(fs, "/proj", 0755), 0);
    CHECK_EQ(ffs_mkdir(fs, "/proj/src", 0755), 0);
    fd = ffs_open(fs, "/proj/src/main.c", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "int main(void){return 0;}\n", 25), 25);
    CHECK_EQ(ffs_lseek(fs, fd, 100000, SEEK_SET), 100000);
    CHECK_EQ(ffs_write(fs, fd, "tail", 4), 4);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    fd = ffs_open(fs, "/proj/src/zero.bin", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, zeros, sizeof(zeros)), (long)sizeof(zeros));
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_link(fs, "/proj/src/main.c", "/proj/src/main.bak"), 0);
    CHECK_EQ(ffs_symlink(fs, "src/main.c", "/proj/entry"), 0);
    CHECK_EQ(ffs_rename(fs, "/proj/src/main.bak", "/proj/src/old.c"), 0);
    CHECK_EQ(ffs_unlink(fs, "/proj/src/old.c"), 0);
    CHECK_EQ(ffs_truncate(fs, "/proj/src/main.c", 10), 0);
}

int main(void)
{
    const char *path = "build/store_invariants.db";
    ffs *fs;
    sqlite3 *db;

    remove(path);
    fs = ffs_create(path);
    CHECK_NOTNULL(fs);
    run_script(fs);
    ffs_destroy(fs);

    db = NULL;
    CHECK_EQ(sqlite3_open(path, &db), SQLITE_OK);
    check_store(db);
    sqlite3_close(db);

    remove(path);
    TEST_DONE();
}
