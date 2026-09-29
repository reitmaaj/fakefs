#include "fakefs_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct split_ctx
{
    char *p;
    int n;
    char *start;
};

struct path_ctx
{
    char *segs[FFS_PATH_MAX / 2];
    int n;
    long parent;
    char *seg;
};

int path_missing(const char *path)
{
    if (path == NULL)
    {
        return 1;
    }
    return path[0] == '\0';
}

int too_long(size_t len)
{
    return len > (size_t)FFS_PATH_MAX;
}

int name_too_long(size_t n)
{
    return n > (size_t)FFS_NAME_MAX;
}

int word_count(int n, int r)
{
    if (r == 1)
    {
        return n;
    }
    return r;
}

long choose_long(int on, long on_true, long on_false)
{
    if (on)
    {
        return on_true;
    }
    return on_false;
}

static int path_valid(const char *path)
{
    if (path_missing(path))
    {
        return -ENOENT;
    }
    return 0;
}

static int len_ok(const char *path)
{
    size_t len;

    len = strlen(path);
    return choose(too_long(len), -ENAMETOOLONG, (int)len);
}

static void sane_if(const char *path, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = len_ok(path);
}

static int path_sane(const char *path)
{
    int r;

    r = path_valid(path);
    sane_if(path, &r);
    return r;
}

static void copy_if_ok(const char *path, char *buf, int r)
{
    if (r < 0)
    {
        return;
    }
    memcpy(buf, path, (size_t)r + 1);
}

int path_copy(const char *path, char *buf)
{
    int r;

    r = path_sane(path);
    copy_if_ok(path, buf, r);
    return r;
}

void path_marks(char *buf, size_t len, int *absolute, int *trailing)
{
    *absolute = buf[0] == '/';
    *trailing = buf[len - 1] == '/';
}

static int is_slash(char ch)
{
    if (ch == '/')
    {
        return 1;
    }
    return 0;
}

static int is_word_end(char ch)
{
    if (ch == '\0')
    {
        return 1;
    }
    return ch == '/';
}

static char *zero_advance(char *p)
{
    *p = '\0';
    return p + 1;
}

static char *skip_slashes(char *p)
{
    while (is_slash(*p))
    {
        p = zero_advance(p);
    }
    return p;
}

static char *word_step(char *p)
{
    char *q;

    q = zero_advance(p);
    return q;
}

static char *term_word(char *p)
{
    if (is_slash(*p) == 0)
    {
        return p;
    }
    p = word_step(p);
    return p;
}

static char *word_end(char *p)
{
    while (is_word_end(*p) == 0)
    {
        ++p;
    }
    p = term_word(p);
    return p;
}

char *scan_word(char *p, char **next)
{
    char *start;

    start = p;
    p = word_end(p);
    *next = p;
    return start;
}

static int comp_len_ok(const struct split_ctx *c)
{
    if (name_too_long(strlen(c->start)))
    {
        return -1;
    }
    return 0;
}

static int comp_slot_ok(const struct split_ctx *c, int maxc)
{
    if (c->n >= maxc)
    {
        return -1;
    }
    return 0;
}

static int comp_bad(int maxc, const struct split_ctx *c)
{
    if (comp_len_ok(c) < 0)
    {
        return 1;
    }
    return comp_slot_ok(c, maxc) < 0;
}

static void comp_store(char **comps, struct split_ctx *c, int *out)
{
    comps[c->n] = c->start;
    ++c->n;
    *out = 0;
}

static void comp_add(char **comps, int maxc, struct split_ctx *c, int *out)
{
    if (comp_bad(maxc, c))
    {
        *out = -ENAMETOOLONG;
        return;
    }
    comp_store(comps, c, out);
}

static int word_at(struct split_ctx *c, char *q)
{
    if (*q == '\0')
    {
        return 1;
    }
    c->start = scan_word(q, &c->p);
    return 0;
}

static int word_next(struct split_ctx *c)
{
    int r;
    char *p;

    p = skip_slashes(c->p);
    r = word_at(c, p);
    return r;
}

static void split_one(char **comps, int maxc, struct split_ctx *c, int *out)
{
    *out = word_next(c);
    if (*out == 0)
    {
        comp_add(comps, maxc, c, out);
    }
}

static struct split_ctx split_begin(char *buf)
{
    struct split_ctx c;

    c.p = buf;
    c.n = 0;
    return c;
}

static void split_run(char **comps, int maxc, struct split_ctx *c, int *r)
{
    while (*r == 0)
    {
        split_one(comps, maxc, c, r);
    }
}

static int split_words(char *buf, char **comps, int maxc)
{
    struct split_ctx c;
    int r;

    c = split_begin(buf);
    r = 0;
    split_run(comps, maxc, &c, &r);
    return word_count(c.n, r);
}

static int words_of(char *buf, size_t len, char **comps, int maxc,
                    int *absolute, int *trailing)
{
    int r;

    path_marks(buf, len, absolute, trailing);
    r = split_words(buf, comps, maxc);
    return r;
}

static void words_if_ok(char *buf, int r, char **comps, int maxc, int *absolute,
                        int *trailing, int *out)
{
    if (r < 0)
    {
        *out = r;
        return;
    }
    *out = words_of(buf, (size_t)r, comps, maxc, absolute, trailing);
}

int path_split(const char *path, char *buf, char **comps, int maxc,
               int *absolute, int *trailing)
{
    int r;

    r = path_copy(path, buf);
    words_if_ok(buf, r, comps, maxc, absolute, trailing, &r);
    return r;
}

static int seg_prep(struct path_ctx *c, long parent) /* LCOV_EXCL_LINE */
{
    if (c->n >= FFS_PATH_MAX / 2)
    {
        return -ENAMETOOLONG; /* LCOV_EXCL_LINE */
    } /* LCOV_EXCL_LINE */
    c->parent = parent;
    return 0; /* LCOV_EXCL_LINE */
}

long alt_code(int r, const long *alt)
{
    if (r < 0)
    {
        return -ENOENT;
    }
    return *alt;
}

static long alt_of(ffs *fs, long ino, struct path_ctx *c)
{
    long alt;
    int r;

    r = dirent_any(fs, ino, &alt, &c->seg);
    return alt_code(r, &alt);
}

static char *seg_alt(ffs *fs, long ino, struct path_ctx *c)
{
    long alt;

    alt = alt_of(fs, ino, c);
    if (alt < 0)
    {
        return NULL;
    }
    c->parent = alt;
    return c->seg;
}

static char *name_or_alt(char *seg, ffs *fs, long ino, struct path_ctx *c)
{
    if (seg != NULL)
    {
        return seg;
    }
    seg = seg_alt(fs, ino, c);
    return seg;
}

static char *seg_name(ffs *fs, long parent, long ino, struct path_ctx *c)
{
    char *seg;

    seg = dirent_name_of(fs, parent, ino);
    seg = name_or_alt(seg, fs, ino, c);
    return seg;
}

static long seg_append(struct path_ctx *c, char *seg)
{
    long parent;

    c->segs[c->n] = seg;
    ++c->n;
    parent = c->parent;
    return parent;
}

static long seg_take(struct path_ctx *c, char *seg)
{
    long r;

    if (seg == NULL)
    {
        return -ENOENT;
    }
    r = seg_append(c, seg);
    return r;
}

static long seg_resolve(ffs *fs, long ino, struct path_ctx *c)
{
    char *seg;
    long r;

    seg = seg_name(fs, c->parent, ino, c);
    r = seg_take(c, seg);
    return r;
}

static long walk_if(ffs *fs, long ino, struct path_ctx *c, long out)
{
    long r;

    if (out < 0)
    {
        return out;
    }
    r = seg_resolve(fs, ino, c);
    return r;
}

static long seg_walk(ffs *fs, long parent, long ino, struct path_ctx *c)
{
    long out;

    out = seg_prep(c, parent);
    out = walk_if(fs, ino, c, out);
    return out;
}

static long up_if(ffs *fs, long ino, struct path_ctx *c, long parent)
{
    long r;

    if (parent < 0)
    {
        return parent;
    }
    r = seg_walk(fs, parent, ino, c);
    return r;
}

static long seg_up(ffs *fs, long ino, struct path_ctx *c)
{
    long parent;
    long r;

    parent = parent_of(fs, ino);
    r = up_if(fs, ino, c, parent);
    return r;
}

static void free_segs(char **segs, int n)
{
    int i;

    for (i = 0; i < n; ++i)
    {
        free(segs[i]);
    }
}

static int segs_error(struct path_ctx *c, long ino)
{
    free_segs(c->segs, c->n);
    return (int)ino;
}

int up_more(ffs *fs, long ino)
{
    return (ino != fs->root) & (ino >= 0);
}

static int collect_code(struct path_ctx *c, long ino)
{
    int r;

    if (ino >= 0)
    {
        return 0;
    }
    r = segs_error(c, ino);
    return r;
}

static int collect_segs(ffs *fs, long ino, struct path_ctx *c)
{
    int r;

    while (up_more(fs, ino))
    {
        ino = seg_up(fs, ino, c);
    }
    r = collect_code(c, ino);
    return r;
}

static int root_mark(char *path)
{
    path[0] = '/';
    path[1] = '\0';
    return 0;
}

int seg_fits(size_t plen, size_t slen)
{
    if (plen + slen + 2 <= FFS_PATH_MAX)
    {
        return 0;
    }
    return -1;
}

static void copy_seg(char *path, size_t plen, const char *seg, size_t slen)
{
    path[plen] = '/';
    memcpy(path + plen + 1, seg, slen + 1);
}

static void seg_copy_if(char *path, size_t plen, const char *seg, size_t slen,
                        int r)
{
    if (r < 0)
    {
        return;
    }
    copy_seg(path, plen, seg, slen);
}

int append_seg(char *path, const char *seg)
{
    size_t plen;
    size_t slen;

    plen = strlen(path);
    slen = strlen(seg);
    seg_copy_if(path, plen, seg, slen, seg_fits(plen, slen));
    return choose(seg_fits(plen, slen) < 0, -1, 0);
}

static int render_segs(char *path, char **segs, int n)
{
    int i;
    int r;

    for (i = n - 1; i >= 0; --i)
    {
        r = append_seg(path, segs[i]);
        if (r < 0)
        {
            return -ENAMETOOLONG; /* LCOV_EXCL_LINE */
        }
    }
    return 0;
}

static int render_choose(char *path, char **segs, int n)
{
    int r;

    if (n == 0)
    {
        r = root_mark(path);
        return r;
    }
    r = render_segs(path, segs, n);
    return r;
}

static int render_path(char *path, char **segs, int n)
{
    int r;

    path[0] = '\0';
    r = render_choose(path, segs, n);
    return r;
}

static void into_copy(char *buf, const char *path, size_t need, int *r)
{
    if (*r < 0)
    {
        return;
    }
    memcpy(buf, path, need);
}

int path_into(char *buf, size_t bufsz, const char *path)
{
    size_t need;
    int r;

    need = strlen(path) + 1;
    r = choose(need > bufsz, -ERANGE, 0);
    into_copy(buf, path, need, &r);
    return r;
}

static void into_if_ok(char *buf, size_t bufsz, char *path, int *r)
{
    if (*r < 0)
    {
        return;
    }
    *r = path_into(buf, bufsz, path);
}

static int render_ctx(char *buf, size_t bufsz, struct path_ctx *c)
{
    char path[FFS_PATH_MAX + 1];
    int r;

    r = render_path(path, c->segs, c->n);
    into_if_ok(buf, bufsz, path, &r);
    return r;
}

static int build_after(char *buf, size_t bufsz, struct path_ctx *c, int r)
{
    int d;

    if (r < 0)
    {
        return r;
    }
    d = render_ctx(buf, bufsz, c);
    free_segs(c->segs, c->n);
    return d;
}

int build_path(ffs *fs, long ino, char *buf, size_t bufsz)
{
    struct path_ctx c;
    int r;

    c.n = 0;
    r = collect_segs(fs, ino, &c);
    r = build_after(buf, bufsz, &c, r);
    return r;
}
