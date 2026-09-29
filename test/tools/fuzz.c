/* Deterministic C fuzz driver for fakefs. Exercises random filesystem
 * operations with a seeded PRNG; intended to run under valgrind/ASan to
 * catch memory errors and crashes. */

#include "fakefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static unsigned long rng_state;

static unsigned long rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static const char *names[] = {"a", "b", "c", "d", "e"};
#define NNAMES 5

static void rand_path(char *buf, size_t sz)
{
    int depth = (int)(rnd() % 3);
    size_t off = 0;
    int i;

    buf[0] = '/';
    off = 1;
    for (i = 0; i <= depth; i++)
    {
        const char *n = names[rnd() % NNAMES];
        size_t l = strlen(n);
        if (off + l + 1 >= sz)
        {
            break;
        }
        memcpy(buf + off, n, l);
        off += l;
        if (i < depth)
        {
            buf[off++] = '/';
        }
    }
    buf[off] = '\0';
}

static void do_write(ffs *fs, const char *path)
{
    char data[64];
    size_t n = (size_t)(rnd() % 48);
    int fd = ffs_open(fs, path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    size_t i;

    if (fd < 0)
    {
        return;
    }
    for (i = 0; i < n; i++)
    {
        data[i] = (char)('a' + (rnd() % 26));
    }
    (void)ffs_write(fs, fd, data, n);
    (void)ffs_close_fd(fs, fd);
}

static void do_read(ffs *fs, const char *path)
{
    char buf[128];
    int fd = ffs_open(fs, path, O_RDONLY, 0);

    if (fd < 0)
    {
        return;
    }
    while (ffs_read(fs, fd, buf, sizeof(buf)) > 0)
    {
    }
    (void)ffs_close_fd(fs, fd);
}

int main(int argc, char **argv)
{
    ffs *fs;
    long i;
    long nops;
    char p1[256];
    char p2[256];

    rng_state = 0x9e3779b97f4a7c15UL;
    nops = (argc > 1) ? atol(argv[1]) : 100000;

    fs = ffs_create_memory();
    if (!fs)
    {
        return 1;
    }

    for (i = 0; i < nops; i++)
    {
        unsigned long op = rnd() % 10;

        switch (op)
        {
        case 0: /* mkdir */
            rand_path(p1, sizeof(p1));
            (void)ffs_mkdir(fs, p1, 0755);
            break;
        case 1: /* create + write + truncate */
            rand_path(p1, sizeof(p1));
            do_write(fs, p1);
            break;
        case 2: /* truncate */
            rand_path(p1, sizeof(p1));
            (void)ffs_truncate(fs, p1, (off_t)(rnd() % 64));
            break;
        case 3: /* unlink */
            rand_path(p1, sizeof(p1));
            (void)ffs_unlink(fs, p1);
            break;
        case 4: /* rmdir */
            rand_path(p1, sizeof(p1));
            (void)ffs_rmdir(fs, p1);
            break;
        case 5: /* rename */
            rand_path(p1, sizeof(p1));
            rand_path(p2, sizeof(p2));
            (void)ffs_rename(fs, p1, p2);
            break;
        case 6: /* link */
            rand_path(p1, sizeof(p1));
            rand_path(p2, sizeof(p2));
            (void)ffs_link(fs, p1, p2);
            break;
        case 7: /* symlink */
            rand_path(p1, sizeof(p1));
            rand_path(p2, sizeof(p2));
            (void)ffs_symlink(fs, p1, p2);
            break;
        case 8: /* read */
            rand_path(p1, sizeof(p1));
            do_read(fs, p1);
            break;
        default: /* chdir */
            rand_path(p1, sizeof(p1));
            (void)ffs_chdir(fs, p1);
            break;
        }
    }

    ffs_destroy(fs);
    printf("fuzz: %ld ops ok\n", nops);
    return 0;
}
