#include "fakefs_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct read_ctx
{
    unsigned char *out;
    size_t len;
    ffs_off pos;
    ffs_off end;
    ffs_off eoff;
    long elen;
    size_t done;
    int r;
};

struct write_ctx
{
    ffs *fs;
    long ino;
    const unsigned char *p;
    ffs_off pos;
    long n;
    int r;
};

struct write_op
{
    ffs_off off;
    ffs_off end;
    const unsigned char *src;
    size_t len;
    unsigned char pre[FFS_FILE_PAGE_SIZE];
    unsigned char suf[FFS_FILE_PAGE_SIZE];
    ffs_off pre_off;
    ffs_off suf_off;
    long pre_len;
    long suf_len;
    int r;
};

long zero_add(long n, unsigned char b)
{
    return n + (b == 0);
}

long zero_count(const unsigned char *buf, long len)
{
    long n;

    for (n = 0; len > 0; --len)
    {
        n = zero_add(n, buf[len - 1]);
    }
    return n;
}

int all_zero(const unsigned char *buf, long len)
{
    return zero_count(buf, len) == len;
}

long min_long(long a, long b)
{
    if (a < b)
    {
        return a;
    }
    return b;
}

size_t min_size(size_t a, size_t b)
{
    if (a < b)
    {
        return a;
    }
    return b;
}

ffs_off min_off(ffs_off a, ffs_off b)
{
    if (a < b)
    {
        return a;
    }
    return b;
}

size_t hole_len(size_t hole, size_t room)
{
    if (hole > room)
    {
        return room;
    }
    return hole;
}

int read_end(int r, ffs_off eoff, ffs_off end)
{
    if (r == 0)
    {
        return 1;
    }
    return eoff >= end;
}

int read_code(int r)
{
    if (r < 0)
    {
        return r;
    }
    return 0;
}

int page_code(int r)
{
    if (r > 0)
    {
        return 0;
    }
    return r;
}

long suf_blen(ffs_off end, ffs_off eoff, long elen)
{
    if (eoff + (ffs_off)elen <= end)
    {
        return 0;
    }
    return (long)((eoff + (ffs_off)elen) - end);
}

ffs_off ext_end_of(ffs_off eoff, long elen, ffs_off end)
{
    return min_off(eoff + (ffs_off)elen, end);
}

int straddle_of(ffs_off xoff, long elen, ffs_off new_size)
{
    if (xoff + (ffs_off)elen <= new_size)
    {
        return 0;
    }
    return 1;
}

long canonical_len(const unsigned char *buf, long len)
{
    for (; len > 0; --len)
    {
        if (buf[len - 1])
        {
            return len;
        }
    }
    return 0;
}

static size_t fill_hole(unsigned char *out, size_t done, size_t len,
                        size_t hole, ffs_off *pos)
{
    size_t h;

    h = hole_len(hole, len - done);
    memset(out + done, 0, h);
    *pos += (ffs_off)h;
    return done + h;
}

static size_t fill_before(unsigned char *out, size_t done, size_t len,
                          ffs_off *pos, ffs_off eoff)
{
    if (eoff > *pos)
    {
        done = fill_hole(out, done, len, (size_t)(eoff - *pos), pos);
    }
    return done;
}

static size_t copy_chunk(const struct read_ctx *c, ffs_off eoff, long elen)
{
    ffs_off ext_end;

    ext_end = ext_end_of(eoff, elen, c->end);
    return min_size((size_t)(ext_end - c->pos), c->len - c->done);
}

static void copy_step(struct read_ctx *c, size_t chunk)
{
    c->pos = c->pos + (ffs_off)chunk;
    c->done = c->done + chunk;
}

static size_t copy_extent(struct read_ctx *c, const unsigned char *tmp,
                          ffs_off eoff, long elen)
{
    size_t chunk;

    chunk = copy_chunk(c, eoff, elen);
    memcpy(c->out + c->done, tmp + (size_t)(c->pos - eoff), chunk);
    copy_step(c, chunk);
    return c->done;
}

static void copy_if(struct read_ctx *c, const unsigned char *tmp, ffs_off eoff,
                    long elen)
{
    if (c->done >= c->len)
    {
        return;
    }
    c->done = copy_extent(c, tmp, eoff, elen);
}

static size_t consume_extent(struct read_ctx *c, ffs_off eoff, long elen,
                             unsigned char *tmp)
{
    c->done = fill_before(c->out, c->done, c->len, &c->pos, eoff);
    copy_if(c, tmp, eoff, elen);
    return c->done;
}

static void read_tail(struct read_ctx *c, int *out)
{
    memset(c->out + c->done, 0, c->len - c->done);
    c->done = c->len;
    *out = 1;
}

static void read_err(const struct read_ctx *c, int *out)
{
    *out = c->r;
}

static void read_stop(struct read_ctx *c, int *out)
{
    if (c->r < 0)
    {
        read_err(c, out);
        return;
    }
    read_tail(c, out);
}

static void read_fetch(ffs *fs, long ino, struct read_ctx *c,
                       unsigned char *tmp)
{
    c->eoff = 0;
    c->elen = 0;
    c->r = extent_next(fs, ino, c->pos, &c->eoff, &c->elen, tmp);
}

static int consume_code(struct read_ctx *c, unsigned char *tmp)
{
    consume_extent(c, c->eoff, c->elen, tmp);
    return 0;
}

static void read_result(struct read_ctx *c, unsigned char *tmp, int *out)
{
    if (read_end(c->r, c->eoff, c->end))
    {
        read_stop(c, out);
        return;
    }
    *out = consume_code(c, tmp);
}

static void read_apply(struct read_ctx *c, unsigned char *tmp, int *out)
{
    if (c->done >= c->len)
    {
        *out = 1;
        return;
    }
    read_result(c, tmp, out);
}

static void read_iter(ffs *fs, long ino, struct read_ctx *c, unsigned char *tmp,
                      int *out)
{
    read_fetch(fs, ino, c, tmp);
    read_apply(c, tmp, out);
}

static void read_init(struct read_ctx *c, void *buf, size_t len, ffs_off off)
{
    c->out = (unsigned char *)buf;
    c->len = len;
    c->pos = off;
    c->end = off + (ffs_off)len;
    c->done = 0;
    c->r = 0;
}

static ssize_t read_finish(const struct read_ctx *c)
{
    if (c->r < 0)
    {
        return -1;
    }
    return (ssize_t)c->done;
}

static void read_all(ffs *fs, long ino, struct read_ctx *c, unsigned char *t)
{
    c->r = 0;
    while (c->r == 0)
    {
        read_iter(fs, ino, c, t, &c->r);
    }
}

ssize_t data_read(ffs *fs, long ino, void *buf, size_t len, ffs_off off)
{
    struct read_ctx c;
    unsigned char tmp[FFS_FILE_PAGE_SIZE];

    read_init(&c, buf, len, off);
    read_all(fs, ino, &c, tmp);
    return read_finish(&c);
}

static int put_code(ffs *fs, long ino, ffs_off off, const unsigned char *buf,
                    long len)
{
    int r;

    r = extent_put(fs, ino, off, buf, len);
    return r;
}

static void chunk_put(ffs *fs, long ino, ffs_off pos, const unsigned char *p,
                      long chunk, int *r)
{
    *r = 0;
    if (all_zero(p, chunk) == 0)
    {
        *r = put_code(fs, ino, pos, p, chunk);
    }
}

static void chunk_step(struct write_ctx *c, long chunk)
{
    c->p = c->p + chunk;
    c->pos = c->pos + (ffs_off)chunk;
    c->n = c->n - chunk;
}

static int run_chunk(struct write_ctx *c)
{
    long chunk;
    int r;

    chunk = min_long(c->n, FFS_FILE_PAGE_SIZE);
    chunk_put(c->fs, c->ino, c->pos, c->p, chunk, &r);
    chunk_step(c, chunk);
    return r;
}

static void write_step(struct write_ctx *c)
{
    if (c->n <= 0)
    {
        c->r = 1;
        return;
    }
    c->r = run_chunk(c);
}

static void write_init(struct write_ctx *c, ffs *fs, long ino,
                       const unsigned char *src, long len, ffs_off start)
{
    c->fs = fs;
    c->ino = ino;
    c->p = src;
    c->pos = start;
    c->n = len;
}

static void write_all(struct write_ctx *c)
{
    c->r = 0;
    while (c->r == 0)
    {
        write_step(c);
    }
}

static int run_page(ffs *fs, long ino, const unsigned char *src, long len,
                    ffs_off start)
{
    struct write_ctx c;

    write_init(&c, fs, ino, src, len, start);
    write_all(&c);
    return page_code(c.r);
}

static int store_run(ffs *fs, long ino, ffs_off start, const unsigned char *src,
                     long len)
{
    int r;

    r = run_page(fs, ino, src, len, start);
    return r;
}

static void write_len(ffs *fs, long ino, ffs_off off, const unsigned char *buf,
                      long len, int *r)
{
    if (all_zero(buf, len) == 0)
    {
        *r = put_code(fs, ino, off, buf, len);
    }
}

static void write_if_nonzero(ffs *fs, long ino, ffs_off off,
                             const unsigned char *buf, long len, int *r)
{
    *r = 0;
    if (len != 0)
    {
        write_len(fs, ino, off, buf, len, r);
    }
}

static int write_back(ffs *fs, long ino, ffs_off off, const unsigned char *buf,
                      long len)
{
    int r;

    write_if_nonzero(fs, ino, off, buf, len, &r);
    return r;
}

static void store_if_ok(ffs *fs, long ino, ffs_off off,
                        const unsigned char *src, long len, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = store_run(fs, ino, off, src, len);
}

static int write_region(ffs *fs, long ino, ffs_off off, ffs_off end,
                        const unsigned char *src, long len)
{
    int r;

    r = extent_delete_overlap(fs, ino, off, end);
    store_if_ok(fs, ino, off, src, len, &r);
    return r;
}

static void write_back_if(ffs *fs, long ino, ffs_off off,
                          const unsigned char *buf, long len, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = write_back(fs, ino, off, buf, len);
}

static int write_back2(ffs *fs, long ino, ffs_off pre_off,
                       const unsigned char *pre, long pre_len, ffs_off suf_off,
                       const unsigned char *suf, long suf_len)
{
    int r;

    r = write_back(fs, ino, pre_off, pre, pre_len);
    write_back_if(fs, ino, suf_off, suf, suf_len, &r);
    return r;
}

static void pre_take(ffs_off off, ffs_off eoff, ffs_off *pre_off, long *pre_len)
{
    if (eoff >= off)
    {
        return;
    }
    *pre_off = eoff;
    *pre_len = (long)(off - eoff);
}

static void pre_take_if(int r, ffs_off off, const ffs_off *eoff,
                        ffs_off *pre_off, long *pre_len)
{
    if (r == 1)
    {
        pre_take(off, *eoff, pre_off, pre_len);
    }
}

static void read_prefix(ffs *fs, long ino, ffs_off off, unsigned char *pre,
                        ffs_off *pre_off, long *pre_len, int *out)
{
    ffs_off eoff;
    long elen;

    *out = extent_at(fs, ino, off, &eoff, &elen, pre);
    pre_take_if(*out, off, &eoff, pre_off, pre_len);
    *out = read_code(*out);
}

static void suf_shift(unsigned char *suf, long elen, long blen)
{
    memmove(suf, suf + (size_t)(elen - blen), (size_t)blen);
}

static void suf_apply(unsigned char *suf, ffs_off end, long elen, long blen,
                      ffs_off *suf_off, long *suf_len)
{
    if (blen == 0)
    {
        return;
    }
    suf_shift(suf, elen, blen);
    *suf_off = end;
    *suf_len = blen;
}

static void suf_straddle(ffs_off end, ffs_off eoff, long elen,
                         unsigned char *suf, ffs_off *suf_off, long *suf_len)
{
    long blen;

    blen = suf_blen(end, eoff, elen);
    suf_apply(suf, end, elen, blen, suf_off, suf_len);
}

static void suf_take_if(int r, ffs_off end, const ffs_off *eoff,
                        const long *elen, unsigned char *suf, ffs_off *suf_off,
                        long *suf_len)
{
    if (r == 1)
    {
        suf_straddle(end, *eoff, *elen, suf, suf_off, suf_len);
    }
}

static void read_suffix(ffs *fs, long ino, ffs_off end, unsigned char *suf,
                        ffs_off *suf_off, long *suf_len, int *out)
{
    ffs_off eoff;
    long elen;

    *out = extent_at(fs, ino, end - 1, &eoff, &elen, suf);
    suf_take_if(*out, end, &eoff, &elen, suf, suf_off, suf_len);
    *out = read_code(*out);
}

static void capture_more(ffs *fs, long ino, ffs_off end, unsigned char *suf,
                         ffs_off *suf_off, long *suf_len, int *r)
{
    if (*r < 0)
    {
        return;
    }
    read_suffix(fs, ino, end, suf, suf_off, suf_len, r);
}

static void capture_edges(ffs *fs, long ino, struct write_op *w)
{
    read_prefix(fs, ino, w->off, w->pre, &w->pre_off, &w->pre_len, &w->r);
    capture_more(fs, ino, w->end, w->suf, &w->suf_off, &w->suf_len, &w->r);
}

static void write_op_init(struct write_op *w, const void *buf, size_t len,
                          ffs_off off)
{
    w->src = (const unsigned char *)buf;
    w->len = len;
    w->off = off;
    w->end = off + (ffs_off)len;
}

static void capture_reset(struct write_op *w)
{
    w->pre_off = 0;
    w->suf_off = 0;
    w->pre_len = 0;
    w->suf_len = 0;
}

static void write_region_if(ffs *fs, long ino, struct write_op *w)
{
    if (w->r < 0)
    {
        return;
    }
    w->r = write_region(fs, ino, w->off, w->end, w->src, (long)w->len);
}

static void write_back2_if(ffs *fs, long ino, struct write_op *w)
{
    if (w->r < 0)
    {
        return;
    }
    w->r = write_back2(fs, ino, w->pre_off, w->pre, w->pre_len, w->suf_off,
                       w->suf, w->suf_len);
}

static void write_run(ffs *fs, long ino, struct write_op *w)
{
    capture_edges(fs, ino, w);
    write_region_if(fs, ino, w);
    write_back2_if(fs, ino, w);
}

int data_write(ffs *fs, long ino, const void *buf, size_t len, ffs_off off)
{
    struct write_op w;

    write_op_init(&w, buf, len, off);
    capture_reset(&w);
    write_run(fs, ino, &w);
    return w.r;
}

static int trunc_straddle(ffs *fs, long ino, ffs_off new_size, ffs_off eoff,
                          const unsigned char *tmp)
{
    long keep;
    int r;

    keep = canonical_len(tmp, (long)(new_size - eoff));
    r = put_code(fs, ino, eoff, tmp, keep);
    return choose(keep == 0, 0, r);
}

static void trunc_tail(ffs *fs, long ino, ffs_off new_size, ffs_off eoff,
                       int straddle, const unsigned char *tmp, int *r)
{
    if (straddle == 0)
    {
        return;
    }
    *r = trunc_straddle(fs, ino, new_size, eoff, tmp);
}

static int trunc_from(ffs *fs, long ino, ffs_off new_size, ffs_off eoff,
                      int straddle, const unsigned char *tmp)
{
    int r;

    r = extent_delete_from(fs, ino, new_size);
    trunc_tail(fs, ino, new_size, eoff, straddle, tmp, &r);
    return r;
}

static void trunc_from_if(ffs *fs, long ino, ffs_off new_size, ffs_off eoff,
                          int straddle, const unsigned char *tmp, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = trunc_from(fs, ino, new_size, eoff, straddle, tmp);
}

static void find_straddle(ffs *fs, long ino, ffs_off new_size, ffs_off *eoff,
                          unsigned char *tmp, int *r)
{
    ffs_off xoff;
    long elen;

    *r = extent_at(fs, ino, new_size - 1, &xoff, &elen, tmp);
    if (*r != 1)
    {
        return;
    }
    *r = straddle_of(xoff, elen, new_size);
    if (*r != 0)
    {
        *eoff = xoff;
    }
}

static int trunc_run(ffs *fs, long ino, ffs_off new_size)
{
    unsigned char tmp[FFS_FILE_PAGE_SIZE];
    ffs_off eoff;
    int r;

    find_straddle(fs, ino, new_size, &eoff, tmp, &r);
    trunc_from_if(fs, ino, new_size, eoff, r, tmp, &r);
    return r;
}

static void trunc_zero_or_run(ffs *fs, long ino, ffs_off new_size, int *r)
{
    if (new_size != 0)
    {
        *r = trunc_run(fs, ino, new_size);
        return;
    }
    *r = extent_delete_all(fs, ino);
}

static int trunc_pick(ffs *fs, long ino, ffs_off new_size)
{
    int r;

    r = -EINVAL;
    if (new_size >= 0)
    {
        trunc_zero_or_run(fs, ino, new_size, &r);
    }
    return r;
}

int data_truncate(ffs *fs, long ino, ffs_off new_size)
{
    int r;

    r = trunc_pick(fs, ino, new_size);
    return r;
}

int data_delete(ffs *fs, long ino)
{
    int r;

    r = extent_delete_all(fs, ino);
    return r;
}
