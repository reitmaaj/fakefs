#include "test.h"
#include "unit.h"

static void helper_checks(void)
{
    long alt;

    CHECK_EQ(path_missing(NULL), 1);
    CHECK_EQ(path_missing(""), 1);
    CHECK_EQ(path_missing("/"), 0);
    CHECK_EQ(too_long((size_t)FFS_PATH_MAX), 0);
    CHECK_EQ(too_long((size_t)FFS_PATH_MAX + 1), 1);
    CHECK_EQ(name_too_long((size_t)FFS_NAME_MAX), 0);
    CHECK_EQ(name_too_long((size_t)FFS_NAME_MAX + 1), 1);
    CHECK_EQ(word_count(3, 1), 3);
    CHECK_EQ(word_count(3, -ENAMETOOLONG), -ENAMETOOLONG);
    CHECK_EQ(choose_long(1, 7, 9), 7);
    CHECK_EQ(choose_long(0, 7, 9), 9);
    alt = 42;
    CHECK_EQ(alt_code(0, &alt), 42);
    CHECK_EQ(alt_code(-ENOENT, &alt), -ENOENT);
}

int main(void)
{
    char buf[FFS_PATH_MAX + 1];
    char *comps[FFS_PATH_MAX / 2];
    char longname[300];
    char longpath[FFS_PATH_MAX + 2];
    int absolute;
    int trailing;
    int n;
    int i;

    helper_checks();
    n = path_split("/a/b/c", buf, comps, FFS_PATH_MAX / 2, &absolute,
                   &trailing);
    CHECK_EQ(n, 3);
    CHECK_EQ(absolute, 1);
    CHECK_EQ(trailing, 0);
    CHECK_STR(comps[0], "a");
    CHECK_STR(comps[1], "b");
    CHECK_STR(comps[2], "c");

    n = path_split("a/b", buf, comps, FFS_PATH_MAX / 2, &absolute, &trailing);
    CHECK_EQ(n, 2);
    CHECK_EQ(absolute, 0);
    CHECK_EQ(trailing, 0);
    CHECK_STR(comps[0], "a");
    CHECK_STR(comps[1], "b");

    n = path_split("//a///b/", buf, comps, FFS_PATH_MAX / 2, &absolute,
                   &trailing);
    CHECK_EQ(n, 2);
    CHECK_EQ(absolute, 1);
    CHECK_EQ(trailing, 1);
    CHECK_STR(comps[0], "a");
    CHECK_STR(comps[1], "b");

    n = path_split("/", buf, comps, FFS_PATH_MAX / 2, &absolute, &trailing);
    CHECK_EQ(n, 0);
    CHECK_EQ(absolute, 1);
    CHECK_EQ(trailing, 1);

    n = path_split("///", buf, comps, FFS_PATH_MAX / 2, &absolute, &trailing);
    CHECK_EQ(n, 0);
    CHECK_EQ(absolute, 1);
    CHECK_EQ(trailing, 1);

    n = path_split("", buf, comps, FFS_PATH_MAX / 2, &absolute, &trailing);
    CHECK_EQ(n, -ENOENT);

    n = path_split(0, buf, comps, FFS_PATH_MAX / 2, &absolute, &trailing);
    CHECK_EQ(n, -ENOENT);

    n = path_split("a/", buf, comps, FFS_PATH_MAX / 2, &absolute, &trailing);
    CHECK_EQ(n, 1);
    CHECK_EQ(trailing, 1);
    CHECK_STR(comps[0], "a");

    for (i = 0; i < 256; i++)
    {
        longname[i] = 'a';
    }
    longname[256] = '\0';
    n = path_split(longname, buf, comps, FFS_PATH_MAX / 2, &absolute,
                   &trailing);
    CHECK_EQ(n, -ENAMETOOLONG);

    for (i = 0; i < FFS_PATH_MAX + 1; i++)
    {
        longpath[i] = 'a';
    }
    longpath[FFS_PATH_MAX + 1] = '\0';
    n = path_split(longpath, buf, comps, FFS_PATH_MAX / 2, &absolute,
                   &trailing);
    CHECK_EQ(n, -ENAMETOOLONG);

    n = path_split("a/b/c", buf, comps, 2, &absolute, &trailing);
    CHECK_EQ(n, -ENAMETOOLONG);

    /* path_copy */
    CHECK_EQ(path_copy("/a/b", buf), 4);
    CHECK_STR(buf, "/a/b");
    CHECK_EQ(path_copy("", buf), -ENOENT);
    CHECK_EQ(path_copy(0, buf), -ENOENT);
    CHECK_EQ(path_copy(longpath, buf), -ENAMETOOLONG);

    /* path_marks */
    strcpy(buf, "/a/b/");
    path_marks(buf, 5, &absolute, &trailing);
    CHECK_EQ(absolute, 1);
    CHECK_EQ(trailing, 1);
    strcpy(buf, "a");
    path_marks(buf, 1, &absolute, &trailing);
    CHECK_EQ(absolute, 0);
    CHECK_EQ(trailing, 0);

    /* scan_word */
    strcpy(buf, "ab/c");
    CHECK_STR(scan_word(buf, &comps[0]), "ab");
    CHECK_STR(comps[0], "c");

    /* seg_fits / append_seg */
    buf[0] = '\0';
    CHECK_EQ(append_seg(buf, "x"), 0);
    CHECK_STR(buf, "/x");
    CHECK_EQ(append_seg(buf, "y"), 0);
    CHECK_STR(buf, "/x/y");
    CHECK_EQ(seg_fits(0, 4), 0);
    CHECK_EQ(seg_fits(4096, 100), -1);

    /* path_into */
    CHECK_EQ(path_into(buf, 8, "/ab"), 0);
    CHECK_STR(buf, "/ab");
    CHECK_EQ(path_into(buf, 8, "/abcdefg"), -ERANGE);

    TEST_DONE();
}
