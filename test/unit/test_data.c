#include "test.h"
#include "unit.h"

static void pure_checks(void)
{
    unsigned char z[4] = {0, 0, 0, 0};
    unsigned char nz[4] = {0, 1, 0, 0};
    unsigned char tail[4] = {1, 2, 0, 0};

    CHECK_EQ(zero_count(z, 4), 4);
    CHECK_EQ(zero_count(nz, 4), 3);
    CHECK_EQ(zero_add(0, 0), 1);
    CHECK_EQ(zero_add(3, 1), 3);
    CHECK_EQ(all_zero(z, 4), 1);
    CHECK_EQ(all_zero(nz, 4), 0);
    CHECK_EQ(canonical_len(tail, 4), 2);
    CHECK_EQ(canonical_len(z, 4), 0);
    CHECK_EQ(min_long(3, 7), 3);
    CHECK_EQ(min_long(7, 3), 3);
    CHECK_EQ(min_size((size_t)3, (size_t)7), (size_t)3);
    CHECK_EQ(min_off((ffs_off)3, (ffs_off)7), (ffs_off)3);
    CHECK_EQ(hole_len(5, 3), (size_t)3);
    CHECK_EQ(hole_len(2, 3), (size_t)2);
    CHECK_EQ(read_end(0, 10, 20), 1);
    CHECK_EQ(read_end(1, 20, 20), 1);
    CHECK_EQ(read_end(1, 19, 20), 0);
    CHECK_EQ(read_code(-EIO), -EIO);
    CHECK_EQ(read_code(1), 0);
    CHECK_EQ(page_code(1), 0);
    CHECK_EQ(page_code(-EIO), -EIO);
    CHECK_EQ(suf_blen(10, 8, 5), (long)3);
    CHECK_EQ(suf_blen(10, 8, 2), (long)0);
    CHECK_EQ(ext_end_of(5, 3, 10), (ffs_off)8);
    CHECK_EQ(ext_end_of(5, 9, 10), (ffs_off)10);
    CHECK_EQ(straddle_of(8, 5, 10), 1);
    CHECK_EQ(straddle_of(8, 2, 10), 0);
}

static void len_checks(void)
{
    CHECK_EQ(extent_len_bad(-1), 1);
    CHECK_EQ(extent_len_bad(0), 0);
    CHECK_EQ(extent_len_bad(FFS_FILE_PAGE_SIZE), 0);
    CHECK_EQ(extent_len_bad(FFS_FILE_PAGE_SIZE + 1), 1);
    CHECK_EQ(extent_put_args_bad(-1, 1), 1);
    CHECK_EQ(extent_put_args_bad(0, 0), 1);
    CHECK_EQ(extent_put_args_bad(0, FFS_FILE_PAGE_SIZE + 1), 1);
    CHECK_EQ(extent_put_args_bad(0, 1), 0);
}

static void extent_empty(ffs *fs, long ino)
{
    ffs_off eoff;
    long elen;
    unsigned char buf[8];

    memset(buf, 0xAB, sizeof(buf));
    CHECK_EQ(extent_at(fs, ino, 0, &eoff, &elen, buf), 0);
    CHECK_EQ(extent_next(fs, ino, 0, &eoff, &elen, buf), 0);
}

static void extent_roundtrip(ffs *fs, long ino)
{
    unsigned char buf[8];
    ffs_off eoff;
    long elen;

    CHECK_EQ(extent_put(fs, ino, 10, (const unsigned char *)"hello", 5), 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(extent_at(fs, ino, 10, &eoff, &elen, buf), 1);
    CHECK_EQ(eoff, 10);
    CHECK_EQ(elen, 5);
    CHECK(memcmp(buf, "hello", 5) == 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(extent_at(fs, ino, 12, &eoff, &elen, buf), 1);
    CHECK_EQ(eoff, 10);
    CHECK_EQ(elen, 5);
    CHECK(memcmp(buf, "hello", 5) == 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(extent_next(fs, ino, 0, &eoff, &elen, buf), 1);
    CHECK_EQ(eoff, 10);
    CHECK_EQ(elen, 5);
}

static void extent_put_reject(ffs *fs, long ino)
{
    unsigned char buf[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    CHECK_EQ(extent_put(fs, ino, 0, buf, 0), -EINVAL);
    CHECK_EQ(extent_put(fs, ino, -1, buf, 4), -EINVAL);
    CHECK_EQ(extent_put(fs, ino, 0, buf, (long)FFS_FILE_PAGE_SIZE + 1),
             -EINVAL);
}

static void ext_overlap_test(ffs *fs, long ino)
{
    unsigned char b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    ffs_off eoff;
    long elen;
    unsigned char out[8];

    CHECK_EQ(extent_put(fs, ino, 0, b, 5), 0);
    CHECK_EQ(extent_put(fs, ino, 10, b, 5), 0);
    CHECK_EQ(extent_delete_overlap(fs, ino, 3, 12), 0);
    CHECK_EQ(extent_next(fs, ino, 0, &eoff, &elen, out), 0);
}

static void ext_from_test(ffs *fs, long ino)
{
    unsigned char b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    ffs_off eoff;
    long elen;
    unsigned char out[8];

    CHECK_EQ(extent_put(fs, ino, 0, b, 5), 0);
    CHECK_EQ(extent_put(fs, ino, 10, b, 5), 0);
    CHECK_EQ(extent_delete_from(fs, ino, 8), 0);
    CHECK_EQ(extent_next(fs, ino, 0, &eoff, &elen, out), 1);
    CHECK_EQ(eoff, 0);
    CHECK_EQ(elen, 5);
    CHECK_EQ(extent_next(fs, ino, 6, &eoff, &elen, out), 0);
}

static void data_single_extent(ffs *fs, long ino)
{
    char out[16];

    CHECK_EQ(data_write(fs, ino, "hello", 5, 10), 0);
    memset(out, 0, sizeof(out));
    CHECK_EQ(data_read(fs, ino, out, 5, 10), 5);
    CHECK(memcmp(out, "hello", 5) == 0);
}

static void data_cross_cap(ffs *fs, long ino)
{
    size_t total = (size_t)FFS_FILE_PAGE_SIZE + 100;
    unsigned char *in = (unsigned char *)malloc(total);
    unsigned char *out = (unsigned char *)malloc(total);
    long i;

    CHECK_NOTNULL(in);
    CHECK_NOTNULL(out);
    for (i = 0; i < (long)total; i++)
    {
        in[i] = (unsigned char)((i * 7 + 3) & 0xFF);
    }
    CHECK_EQ(data_write(fs, ino, in, total, 0), 0);
    memset(out, 0, total);
    CHECK_EQ(data_read(fs, ino, out, total, 0), (ssize_t)total);
    CHECK(memcmp(in, out, total) == 0);
    free(in);
    free(out);
}

static void data_partial_overwrite(ffs *fs, long ino)
{
    char out[10];

    CHECK_EQ(data_write(fs, ino, "abcdefghij", 10, 0), 0);
    CHECK_EQ(data_write(fs, ino, "XY", 2, 4), 0);
    memset(out, 0, sizeof(out));
    CHECK_EQ(data_read(fs, ino, out, 10, 0), 10);
    CHECK(memcmp(out, "abcdXYghij", 10) == 0);
}

static void data_split_preserves(ffs *fs, long ino)
{
    unsigned char tmp[FFS_FILE_PAGE_SIZE];
    ffs_off eoff;
    long elen;
    char out[10];

    CHECK_EQ(data_write(fs, ino, "abcdefghij", 10, 0), 0);
    CHECK_EQ(data_write(fs, ino, "X", 1, 4), 0);
    memset(out, 0, sizeof(out));
    CHECK_EQ(data_read(fs, ino, out, 10, 0), 10);
    CHECK(memcmp(out, "abcdXfghij", 10) == 0);
    CHECK_EQ(extent_next(fs, ino, 0, &eoff, &elen, tmp), 1);
    CHECK_EQ(eoff, 0);
    CHECK_EQ(elen, 4);
    CHECK_EQ(extent_next(fs, ino, 5, &eoff, &elen, tmp), 1);
    CHECK_EQ(eoff, 5);
    CHECK_EQ(elen, 5);
    CHECK_EQ(extent_next(fs, ino, 11, &eoff, &elen, tmp), 0);
}

static void data_sparse_write(ffs *fs, long ino)
{
    ffs_off off = (ffs_off)FFS_FILE_PAGE_SIZE * 4 + 7;
    char out[4];

    CHECK_EQ(data_write(fs, ino, "z", 1, off), 0);
    memset(out, 0, sizeof(out));
    CHECK_EQ(data_read(fs, ino, out, 1, off), 1);
    CHECK_EQ(out[0], 'z');
    CHECK_EQ(data_read(fs, ino, out, 1, 0), 1);
    CHECK_EQ(out[0], 0);
}

static void data_truncate_no_resurrect(ffs *fs, long ino)
{
    size_t total = (size_t)FFS_FILE_PAGE_SIZE + 50;
    unsigned char *in = (unsigned char *)malloc(total);
    unsigned char *out = (unsigned char *)malloc(total);
    long i;

    CHECK_NOTNULL(in);
    CHECK_NOTNULL(out);
    for (i = 0; i < (long)total; i++)
    {
        in[i] = (unsigned char)((i + 1) & 0xFF);
    }
    CHECK_EQ(data_write(fs, ino, in, total, 0), 0);

    CHECK_EQ(data_truncate(fs, ino, FFS_FILE_PAGE_SIZE + 10), 0);
    memset(out, 0xEE, total);
    CHECK_EQ(data_read(fs, ino, out, total, 0), (ssize_t)total);
    for (i = 0; i < (long)total; i++)
    {
        if (i < (long)(FFS_FILE_PAGE_SIZE + 10))
        {
            CHECK_EQ(out[i], in[i]);
        }
        else
        {
            CHECK_EQ(out[i], 0);
        }
    }
    free(in);
    free(out);
}

static void data_truncate_zero(ffs *fs, long ino)
{
    ffs_off eoff;
    long elen;
    unsigned char buf[8];
    unsigned char tmp[8];

    CHECK_EQ(data_write(fs, ino, "abcdefgh", 8, 0), 0);
    CHECK_EQ(data_truncate(fs, ino, 0), 0);
    CHECK_EQ(data_read(fs, ino, buf, 8, 0), 8);
    CHECK_EQ(buf[0], 0);
    CHECK_EQ(extent_next(fs, ino, 0, &eoff, &elen, tmp), 0);
}

static void t_delete(ffs *fs, long ino)
{
    ffs_off eoff;
    long elen;
    unsigned char tmp[8];
    char out[4];

    CHECK_EQ(data_write(fs, ino, "abcd", 4, 0), 0);
    CHECK_EQ(data_delete(fs, ino), 0);
    memset(out, 0, sizeof(out));
    CHECK_EQ(data_read(fs, ino, out, 4, 0), 4);
    CHECK_EQ(out[0], 0);
    CHECK_EQ(extent_next(fs, ino, 0, &eoff, &elen, tmp), 0);
}

static void data_cap_invariant(ffs *fs, long ino)
{
    size_t total = (size_t)FFS_FILE_PAGE_SIZE * 2 + 5;
    unsigned char *in = (unsigned char *)malloc(total);
    unsigned char *out = (unsigned char *)malloc(total);
    unsigned char tmp[FFS_FILE_PAGE_SIZE];
    ffs_off eoff;
    ffs_off pos;
    long elen;
    long n = 0;
    long i;

    CHECK_NOTNULL(in);
    CHECK_NOTNULL(out);
    for (i = 0; i < (long)total; i++)
    {
        in[i] = (unsigned char)((i * 3 + 1) & 0xFF);
    }
    CHECK_EQ(data_write(fs, ino, in, total, 0), 0);
    pos = 0;
    while (extent_next(fs, ino, pos, &eoff, &elen, tmp) == 1)
    {
        CHECK(elen >= 1 && elen <= (long)FFS_FILE_PAGE_SIZE);
        CHECK(eoff >= pos);
        pos = eoff + (ffs_off)elen;
        n++;
    }
    CHECK(n >= 3);
    memset(out, 0, total);
    CHECK_EQ(data_read(fs, ino, out, total, 0), (ssize_t)total);
    CHECK(memcmp(in, out, total) == 0);
    free(in);
    free(out);
}

int main(void)
{
    ffs *fs;
    long ino = 42L;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);

    len_checks();
    pure_checks();
    extent_empty(fs, ino++);
    extent_roundtrip(fs, ino++);
    extent_put_reject(fs, ino++);
    ext_overlap_test(fs, ino++);
    ext_from_test(fs, ino++);
    data_single_extent(fs, ino++);
    data_cross_cap(fs, ino++);
    data_partial_overwrite(fs, ino++);
    data_split_preserves(fs, ino++);
    data_sparse_write(fs, ino++);
    data_truncate_no_resurrect(fs, ino++);
    data_truncate_zero(fs, ino++);
    t_delete(fs, ino++);
    data_cap_invariant(fs, ino++);

    ffs_destroy(fs);
    TEST_DONE();
}
