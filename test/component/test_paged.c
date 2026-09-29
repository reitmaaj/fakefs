#include "test.h"
#include "fakefs.h"

#include <sqlite3.h>

#define TEST_DB "build/ffs_paged_test.db"
#define P 65536L

static long page_count(const char *path, long ino)
{
    sqlite3 *db;
    sqlite3_stmt *s;
    long n = -1;

    if (sqlite3_open(path, &db) != SQLITE_OK)
    {
        return -1;
    }
    if (sqlite3_prepare_v2(db,
                           "SELECT COUNT(*) FROM file_extents WHERE ino = ?",
                           -1, &s, 0) == SQLITE_OK)
    {
        sqlite3_bind_int64(s, 1, (sqlite3_int64)ino);
        if (sqlite3_step(s) == SQLITE_ROW)
        {
            n = (long)sqlite3_column_int64(s, 0);
        }
        sqlite3_finalize(s);
    }
    sqlite3_close(db);
    return n;
}

static long page_len(const char *path, long ino, long off)
{
    sqlite3 *db;
    sqlite3_stmt *s;
    long n = 0;

    if (sqlite3_open(path, &db) != SQLITE_OK)
    {
        return -1;
    }
    if (sqlite3_prepare_v2(db,
                           "SELECT length(data) FROM file_extents "
                           "WHERE ino = ? AND off = ?",
                           -1, &s, 0) == SQLITE_OK)
    {
        sqlite3_bind_int64(s, 1, (sqlite3_int64)ino);
        sqlite3_bind_int64(s, 2, (sqlite3_int64)off);
        if (sqlite3_step(s) == SQLITE_ROW && sqlite3_column_type(s, 0) !=
                                                 SQLITE_NULL)
        {
            n = (long)sqlite3_column_int64(s, 0);
        }
        sqlite3_finalize(s);
    }
    sqlite3_close(db);
    return n;
}

static void empty_file(ffs *fs)
{
    struct ffs_stat st;
    int fd;
    char c;

    fd = ffs_open(fs, "/e", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_stat(fs, "/e", &st), 0);
    CHECK_EQ(st.st_size, 0);
    fd = ffs_open(fs, "/e", O_RDONLY, 0);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_read(fs, fd, &c, 1), 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
}

static void one_page_roundtrip(ffs *fs)
{
    struct ffs_stat st;
    int fd;
    char buf[16];

    fd = ffs_open(fs, "/a", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "hello paged", 11), 11);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_stat(fs, "/a", &st), 0);
    CHECK_EQ(st.st_size, 11);
    fd = ffs_open(fs, "/a", O_RDONLY, 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, sizeof(buf)), 11);
    CHECK_STR(buf, "hello paged");
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
}

static void cross_page(ffs *fs)
{
    size_t total = (size_t)P + 100;
    unsigned char *in = (unsigned char *)malloc(total);
    unsigned char *out = (unsigned char *)malloc(total);
    struct ffs_stat st;
    int fd;
    long i;

    CHECK_NOTNULL(in);
    CHECK_NOTNULL(out);
    for (i = 0; i < (long)total; i++)
    {
        in[i] = (unsigned char)((i * 13 + 5) & 0xFF);
    }
    fd = ffs_open(fs, "/c", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, in, total), (ssize_t)total);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_stat(fs, "/c", &st), 0);
    CHECK_EQ(st.st_size, (unsigned long)total);
    fd = ffs_open(fs, "/c", O_RDONLY, 0);
    memset(out, 0, total);
    CHECK_EQ(ffs_read(fs, fd, out, total), (ssize_t)total);
    CHECK(memcmp(in, out, total) == 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    free(in);
    free(out);
}

static void partial_overwrite(ffs *fs)
{
    int fd;
    char buf[10];

    fd = ffs_open(fs, "/p", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "abcdefghij", 10), 10);
    CHECK_EQ(ffs_lseek(fs, fd, 4, SEEK_SET), 4);
    CHECK_EQ(ffs_write(fs, fd, "XY", 2), 2);
    CHECK_EQ(ffs_lseek(fs, fd, 0, SEEK_SET), 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, 10), 10);
    CHECK(memcmp(buf, "abcdXYghij", 10) == 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
}

static void sparse_seek_write(ffs *fs)
{
    struct ffs_stat st;
    long ino;
    int fd;
    char buf[4];
    off_t off = (off_t)P * 2 + 7;

    fd = ffs_open(fs, "/sp", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_lseek(fs, fd, off, SEEK_SET), off);
    CHECK_EQ(ffs_write(fs, fd, "z", 1), 1);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_stat(fs, "/sp", &st), 0);
    CHECK_EQ(st.st_size, (unsigned long)(off + 1));
    ino = (long)st.st_ino;
    CHECK(page_count(TEST_DB, ino) <= 2);
    fd = ffs_open(fs, "/sp", O_RDONLY, 0);
    CHECK_EQ(ffs_lseek(fs, fd, off, SEEK_SET), off);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, 1), 1);
    CHECK_EQ(buf[0], 'z');
    CHECK_EQ(ffs_lseek(fs, fd, 0, SEEK_SET), 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, 1), 1);
    CHECK_EQ(buf[0], 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
}

static void sparse_pwrite(ffs *fs)
{
    struct ffs_stat st;
    int fd;
    off_t off = (off_t)P * 3 + 11;

    fd = ffs_open(fs, "/pw", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_pwrite(fs, fd, "q", 1, off), 1);
    CHECK_EQ(ffs_stat(fs, "/pw", &st), 0);
    CHECK_EQ(st.st_size, (unsigned long)(off + 1));
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
}

static void truncate_extend(ffs *fs)
{
    struct ffs_stat st;
    int fd;
    char c;
    off_t huge = (off_t)1 << 40;

    fd = ffs_open(fs, "/tx", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_ftruncate(fs, fd, huge), 0);
    CHECK_EQ(ffs_stat(fs, "/tx", &st), 0);
    CHECK_EQ(st.st_size, (unsigned long)huge);
    CHECK_EQ(page_count(TEST_DB, (long)st.st_ino), 0);
    CHECK_EQ(ffs_lseek(fs, fd, huge - 1, SEEK_SET), huge - 1);
    CHECK_EQ(ffs_read(fs, fd, &c, 1), 1);
    CHECK_EQ(c, 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
}

static void truncate_shrink_grow(ffs *fs)
{
    struct ffs_stat st;
    int fd;
    char buf[100];

    fd = ffs_open(fs, "/tg", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "0123456789ABCDEFGHIJ", 20), 20);
    CHECK_EQ(ffs_ftruncate(fs, fd, 10), 0);
    CHECK_EQ(ffs_ftruncate(fs, fd, 20), 0);
    CHECK_EQ(ffs_lseek(fs, fd, 0, SEEK_SET), 0);
    memset(buf, 0xEE, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, 20), 20);
    CHECK(memcmp(buf, "0123456789", 10) == 0);
    CHECK(memcmp(buf + 10, "\0\0\0\0\0\0\0\0\0\0", 10) == 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_stat(fs, "/tg", &st), 0);
    CHECK_EQ(st.st_size, 20);
}

static void truncate_zero(ffs *fs)
{
    struct ffs_stat st;
    int fd;
    long ino;

    fd = ffs_open(fs, "/tz", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "abcdefgh", 8), 8);
    CHECK_EQ(ffs_ftruncate(fs, fd, 0), 0);
    CHECK_EQ(ffs_stat(fs, "/tz", &st), 0);
    CHECK_EQ(st.st_size, 0);
    ino = (long)st.st_ino;
    CHECK_EQ(page_count(TEST_DB, ino), 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
}

static void shrink_inside_page(ffs *fs)
{
    struct ffs_stat st;
    long ino;
    int fd;

    fd = ffs_open(fs, "/si", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "abcdefghij", 10), 10);
    CHECK_EQ(ffs_ftruncate(fs, fd, 3), 0);
    CHECK_EQ(ffs_stat(fs, "/si", &st), 0);
    CHECK_EQ(st.st_size, 3);
    ino = (long)st.st_ino;
    CHECK_EQ(page_len(TEST_DB, ino, 0), 3);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
}

static void zero_page_elision(ffs *fs)
{
    struct ffs_stat st;
    long ino;
    int fd;
    char *z = (char *)malloc((size_t)P);

    CHECK_NOTNULL(z);
    memset(z, 0, (size_t)P);
    fd = ffs_open(fs, "/ze", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, z, (size_t)P), (ssize_t)P);
    CHECK_EQ(ffs_stat(fs, "/ze", &st), 0);
    ino = (long)st.st_ino;
    CHECK_EQ(page_len(TEST_DB, ino, 0), 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    free(z);
}

static void hard_links(ffs *fs)
{
    struct ffs_stat st;
    long ino;
    int fd;
    char buf[16];

    fd = ffs_open(fs, "/hl", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "linked-data", 11), 11);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(ffs_link(fs, "/hl", "/hl2"), 0);
    CHECK_EQ(ffs_stat(fs, "/hl", &st), 0);
    ino = (long)st.st_ino;
    CHECK_EQ(page_count(TEST_DB, ino), 1);
    fd = ffs_open(fs, "/hl2", O_RDONLY, 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, 11), 11);
    CHECK_STR(buf, "linked-data");
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_unlink(fs, "/hl"), 0);
    CHECK_EQ(page_count(TEST_DB, ino), 1);
    fd = ffs_open(fs, "/hl2", O_RDONLY, 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, 11), 11);
    CHECK_STR(buf, "linked-data");
    CHECK_EQ(ffs_close_fd(fs, fd), 0);

    CHECK_EQ(ffs_unlink(fs, "/hl2"), 0);
    CHECK_EQ(page_count(TEST_DB, ino), 0);
}

static void unlink_open(ffs *fs)
{
    struct ffs_stat st;
    long ino;
    int fd;
    char buf[8];

    fd = ffs_open(fs, "/uo", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "persist", 7), 7);
    CHECK_EQ(ffs_stat(fs, "/uo", &st), 0);
    ino = (long)st.st_ino;
    CHECK_EQ(ffs_unlink(fs, "/uo"), 0);
    CHECK_EQ(page_count(TEST_DB, ino), 1);
    CHECK_EQ(ffs_lseek(fs, fd, 0, SEEK_SET), 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, 7), 7);
    CHECK_STR(buf, "persist");
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    CHECK_EQ(page_count(TEST_DB, ino), 0);
}

static void pread_pwrite_offset(ffs *fs)
{
    int fd;
    char buf[8];

    fd = ffs_open(fs, "/po", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_pwrite(fs, fd, "abcd", 4, 0), 4);
    CHECK_EQ(ffs_lseek(fs, fd, 0, SEEK_CUR), 0);
    CHECK_EQ(ffs_pwrite(fs, fd, "XY", 2, 2), 2);
    CHECK_EQ(ffs_lseek(fs, fd, 0, SEEK_CUR), 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_pread(fs, fd, buf, 4, 0), 4);
    CHECK(memcmp(buf, "abXY", 4) == 0);
    CHECK_EQ(ffs_lseek(fs, fd, 0, SEEK_CUR), 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
}

static void append_mode(ffs *fs)
{
    int fd;
    char buf[4];

    fd = ffs_open(fs, "/ap", O_CREAT | O_WRONLY | O_APPEND, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "a", 1), 1);
    CHECK_EQ(ffs_lseek(fs, fd, 0, SEEK_SET), 0);
    CHECK_EQ(ffs_write(fs, fd, "b", 1), 1);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
    fd = ffs_open(fs, "/ap", O_RDONLY, 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ffs_read(fs, fd, buf, sizeof(buf)), 2);
    CHECK(memcmp(buf, "ab", 2) == 0);
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
}

static void large_offsets(ffs *fs)
{
    struct ffs_stat st;
    int fd;
    char c;
    off_t g2 = (off_t)2 << 30;
    off_t g4 = (off_t)4 << 30;

    fd = ffs_open(fs, "/big", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_pwrite(fs, fd, "A", 1, g2 + 5), 1);
    CHECK_EQ(ffs_pwrite(fs, fd, "B", 1, g4 + 9), 1);
    CHECK_EQ(ffs_stat(fs, "/big", &st), 0);
    CHECK_EQ(st.st_size, (unsigned long)(g4 + 10));
    CHECK_EQ(ffs_pread(fs, fd, &c, 1, g2 + 5), 1);
    CHECK_EQ(c, 'A');
    CHECK_EQ(ffs_pread(fs, fd, &c, 1, g4 + 9), 1);
    CHECK_EQ(c, 'B');
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
}

static void atime_metadata(ffs *fs)
{
    struct ffs_stat st;
    int fd;
    char buf[8];

    CHECK_EQ(ffs_setatime(fs, 1), 0);
    fd = ffs_open(fs, "/am", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0);
    CHECK_EQ(ffs_write(fs, fd, "touchme", 7), 7);
    CHECK_EQ(ffs_stat(fs, "/am", &st), 0);
    {
        time_t before = st.st_atime;
        CHECK_EQ(ffs_lseek(fs, fd, 0, SEEK_SET), 0);
        CHECK_EQ(ffs_read(fs, fd, buf, 7), 7);
        CHECK_EQ(ffs_stat(fs, "/am", &st), 0);
        CHECK(st.st_atime >= before);
    }
    CHECK_EQ(ffs_close_fd(fs, fd), 0);
}

int main(void)
{
    ffs *fs;

    remove(TEST_DB);
    fs = ffs_create(TEST_DB);
    CHECK_NOTNULL(fs);

    empty_file(fs);
    one_page_roundtrip(fs);
    cross_page(fs);
    partial_overwrite(fs);
    sparse_seek_write(fs);
    sparse_pwrite(fs);
    truncate_extend(fs);
    truncate_shrink_grow(fs);
    truncate_zero(fs);
    shrink_inside_page(fs);
    zero_page_elision(fs);
    hard_links(fs);
    unlink_open(fs);
    pread_pwrite_offset(fs);
    append_mode(fs);
    large_offsets(fs);
    atime_metadata(fs);

    ffs_destroy(fs);
    remove(TEST_DB);
    TEST_DONE();
}
