#include "test.h"
#include "unit.h"

static void insert_code_checks(void)
{
    CHECK_EQ(insert_code(SQLITE_DONE), 0);
    CHECK_EQ(insert_code(SQLITE_CONSTRAINT), -EEXIST);
    CHECK_EQ(insert_code(SQLITE_ERROR), -EIO);
}

static void inode_helper_checks(ffs *fs)
{
    struct finode fi;

    CHECK_EQ(q_code(-2, -2), -2);
    CHECK_EQ(q_code(5, -2), 0);
    CHECK_EQ(kind_is_text(SQLITE_TEXT), 1);
    CHECK_EQ(kind_is_text(SQLITE_INTEGER), 0);
    CHECK_EQ(target_code("x"), 0);
    CHECK_EQ(target_code(NULL), -ENOMEM);
    CHECK_EQ(no_refs(fs, 1), 1);
    memset(&fi, 0, sizeof(fi));
    fi.mode = (long)(FFS_S_IFDIR | 0755UL);
    CHECK_EQ(trunc_dir_ok(&fi), -EISDIR);
    fi.mode = (long)(FFS_S_IFREG | 0644UL);
    CHECK_EQ(trunc_dir_ok(&fi), 0);
}

int main(void)
{
    ffs *fs;
    struct finode fi;
    struct finode got;
    struct finode sl;
    long ino;
    long sino;
    long mode;
    long p;
    char *tgt;
    char *nm;

    fs = ffs_create_memory();
    CHECK_NOTNULL(fs);

    insert_code_checks();
    inode_helper_checks(fs);
    memset(&fi, 0, sizeof(fi));
    fi.parent = 1;
    fi.mode = (long)(FFS_S_IFREG | 0644UL);
    fi.uid = 1000;
    fi.gid = 2000;
    fi.nlink = 1;
    fi.size = 0;

    ino = inode_new(fs, &fi);
    CHECK(ino > 0);

    CHECK_EQ(inode_load(fs, ino, &got), 0);
    CHECK_EQ(got.ino, ino);
    CHECK_EQ(got.parent, 1);
    CHECK_EQ(got.mode, (long)(FFS_S_IFREG | 0644UL));
    CHECK_EQ(got.uid, 1000);
    CHECK_EQ(got.gid, 2000);
    CHECK_EQ(got.nlink, 1);
    CHECK_EQ(got.size, 0);
    inode_free(&got);

    CHECK_EQ(inode_load(fs, 99999, &got), -ENOENT);

    CHECK_EQ(dirent_insert(fs, 1, "f", ino), 0);
    CHECK_EQ(dirent_lookup(fs, 1, "f"), ino);
    CHECK_EQ(dirent_lookup(fs, 1, "nope"), -ENOENT);
    CHECK_EQ(dirent_insert(fs, 1, "f", ino), -EEXIST);
    CHECK(dir_count(fs, 1) >= 1);

    nm = dirent_name_of(fs, 1, ino);
    CHECK_NOTNULL(nm);
    CHECK_STR(nm, "f");
    free(nm);

    CHECK_EQ(inode_mode(fs, ino, &mode), 0);
    CHECK_EQ(mode, (long)(FFS_S_IFREG | 0644UL));
    CHECK_EQ(inode_mode(fs, 99999, &mode), -ENOENT);
    CHECK_EQ(inode_parent(fs, ino, &p), 0);
    CHECK_EQ(p, 1);

    memset(&sl, 0, sizeof(sl));
    sl.parent = 1;
    sl.mode = (long)(FFS_S_IFLNK | 0777UL);
    sl.uid = 0;
    sl.gid = 0;
    sl.nlink = 1;
    sl.size = 3;
    sl.target = (char *)"abc";
    sino = inode_new(fs, &sl);
    CHECK(sino > 0);
    tgt = 0;
    CHECK_EQ(inode_target(fs, sino, &tgt), 0);
    CHECK_NOTNULL(tgt);
    CHECK_STR(tgt, "abc");
    free(tgt);
    tgt = 0;
    CHECK_EQ(inode_target(fs, ino, &tgt), -ENOENT);

    CHECK_EQ(is_dir(fs, ino), 0);
    CHECK_EQ(is_dir(fs, 1), 1);

    CHECK_EQ(dir_nlink(fs, ino, 1), 0);
    CHECK_EQ(inode_load(fs, ino, &got), 0);
    CHECK_EQ(got.nlink, 2);
    got.uid = 7;
    CHECK_EQ(inode_store(fs, &got), 0);
    inode_free(&got);
    CHECK_EQ(inode_load(fs, ino, &got), 0);
    CHECK_EQ(got.uid, 7);
    CHECK_EQ(got.nlink, 2);
    inode_free(&got);

    CHECK_EQ(dir_touch(fs, ino, 12345), 0);
    CHECK_EQ(inode_load(fs, ino, &got), 0);
    CHECK_EQ(got.mtime, 12345);
    CHECK_EQ(got.ctime, 12345);
    inode_free(&got);

    CHECK_EQ(dirent_delete(fs, 1, "f"), 0);
    CHECK_EQ(dirent_lookup(fs, 1, "f"), -ENOENT);

    CHECK_EQ(inode_delete(fs, ino), 0);
    CHECK_EQ(inode_load(fs, ino, &got), -ENOENT);

    /* mode_is_dir */
    CHECK_EQ(mode_is_dir(FFS_S_IFDIR | 0755L), 1);
    CHECK_EQ(mode_is_dir(FFS_S_IFREG | 0644L), 0);
    CHECK_EQ(mode_is_dir(FFS_S_IFLNK | 0777UL), 0);

    ffs_destroy(fs);
    TEST_DONE();
}
