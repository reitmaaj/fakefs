#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Mode predicates on ffs_stat.st_mode; the owning public header exposes only
   the FFS_S_IF* object-like mode-bit constants, not these predicates. */
#define FFS_S_ISDIR(m) (((m) & FFS_S_IFMT) == FFS_S_IFDIR)
#define FFS_S_ISREG(m) (((m) & FFS_S_IFMT) == FFS_S_IFREG)
#define FFS_S_ISLNK(m) (((m) & FFS_S_IFMT) == FFS_S_IFLNK)

static int test_failures = 0;
static int test_checks = 0;

#define CHECK(cond)                                                            \
    do                                                                         \
    {                                                                          \
        test_checks++;                                                         \
        if (!(cond))                                                           \
        {                                                                      \
            test_failures++;                                                   \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do                                                                         \
    {                                                                          \
        long _a = (long)(a);                                                   \
        long _b = (long)(b);                                                   \
        test_checks++;                                                         \
        if (_a != _b)                                                          \
        {                                                                      \
            test_failures++;                                                   \
            fprintf(stderr, "FAIL %s:%d: %s == %s (%ld != %ld)\n", __FILE__,   \
                    __LINE__, #a, #b, _a, _b);                                 \
        }                                                                      \
    } while (0)

#define CHECK_STR(a, b)                                                        \
    do                                                                         \
    {                                                                          \
        const char *_a = (a);                                                  \
        const char *_b = (b);                                                  \
        test_checks++;                                                         \
        if (_a == 0 || _b == 0 || strcmp(_a, _b) != 0)                         \
        {                                                                      \
            test_failures++;                                                   \
            fprintf(stderr, "FAIL %s:%d: strcmp(%s, %s) (\"%s\" != \"%s\")\n", \
                    __FILE__, __LINE__, #a, #b, _a ? _a : "(null)",            \
                    _b ? _b : "(null)");                                       \
        }                                                                      \
    } while (0)

#define CHECK_ERRNO(call, exp)                                                 \
    do                                                                         \
    {                                                                          \
        int _r = (call);                                                       \
        test_checks++;                                                         \
        if (_r != -1 || errno != (exp))                                        \
        {                                                                      \
            test_failures++;                                                   \
            fprintf(stderr, "FAIL %s:%d: %s -> errno %d (want %d, ret %d)\n",  \
                    __FILE__, __LINE__, #call, errno, (exp), _r);              \
        }                                                                      \
    } while (0)

#define CHECK_NE(a, b)                                                         \
    do                                                                         \
    {                                                                          \
        long _a = (long)(a);                                                   \
        long _b = (long)(b);                                                   \
        test_checks++;                                                         \
        if (_a == _b)                                                          \
        {                                                                      \
            test_failures++;                                                   \
            fprintf(stderr, "FAIL %s:%d: %s != %s (%ld == %ld)\n", __FILE__,   \
                    __LINE__, #a, #b, _a, _b);                                 \
        }                                                                      \
    } while (0)

#define CHECK_NOTNULL(p)                                                       \
    do                                                                         \
    {                                                                          \
        test_checks++;                                                         \
        if ((p) == 0)                                                          \
        {                                                                      \
            test_failures++;                                                   \
            fprintf(stderr, "FAIL %s:%d: %s != NULL\n", __FILE__, __LINE__,    \
                    #p);                                                       \
        }                                                                      \
    } while (0)

#define CHECK_NULL(p)                                                          \
    do                                                                         \
    {                                                                          \
        test_checks++;                                                         \
        if ((p) != 0)                                                          \
        {                                                                      \
            test_failures++;                                                   \
            fprintf(stderr, "FAIL %s:%d: %s == NULL\n", __FILE__, __LINE__,    \
                    #p);                                                       \
        }                                                                      \
    } while (0)

#define CHECK_EQ_ERRNO(call, exp_ret, exp_errno)                               \
    do                                                                         \
    {                                                                          \
        long _r = (long)(call);                                                \
        test_checks++;                                                         \
        if (_r != (long)(exp_ret) || errno != (exp_errno))                     \
        {                                                                      \
            test_failures++;                                                   \
            fprintf(stderr,                                                    \
                    "FAIL %s:%d: %s -> ret %ld/errno %d (want %ld/%d)\n",      \
                    __FILE__, __LINE__, #call, _r, errno, (long)(exp_ret),     \
                    (exp_errno));                                              \
        }                                                                      \
    } while (0)

#define FAIL(msg)                                                              \
    do                                                                         \
    {                                                                          \
        test_checks++;                                                         \
        test_failures++;                                                       \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));        \
    } while (0)

#define TEST_DONE()                                                            \
    do                                                                         \
    {                                                                          \
        if (test_failures)                                                     \
        {                                                                      \
            fprintf(stderr, "%d/%d checks failed\n", test_failures,            \
                    test_checks);                                              \
            return 1;                                                          \
        }                                                                      \
        printf("ok (%d checks)\n", test_checks);                               \
        return 0;                                                              \
    } while (0)

#endif
