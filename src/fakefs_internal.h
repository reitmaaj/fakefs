#ifndef FAKEFS_INTERNAL_H
#define FAKEFS_INTERNAL_H

#include "fakefs.h"

#include <sqlite3.h>

#define FFS_NAME_MAX 255
#define FFS_PATH_MAX 4096
#define FFS_MAX_FD 64
#define FFS_MAX_SYMLINK 40
#define FFS_MAX_COMPS (FFS_PATH_MAX / 2)

/* Regular-file extent cap (part of the database format). */
#define FFS_FILE_PAGE_SIZE 65536L

/* Portable signed 64-bit file offset/size (SQLite's int64, C89-safe). */
typedef sqlite3_int64 ffs_off;

#define FFS_ROOT_INO 1L
#define FFS_ROOT_MODE (FFS_S_IFDIR | 0755L)
#define FFS_DEFAULT_UMASK 022UL
#define FFS_ALL_PERM 0777UL
#define FFS_MODE_MASK_ALL 07777UL
#define FFS_DIR_POS_START (-2)
#define FFS_DIR_POS_DOT (-1)
#define FFS_DIR_NAME_SZ 256

#define FFS_PERM_BITS 7L
#define FFS_EXEC_ANY 0111L
#define FFS_SHIFT_OWNER 6
#define FFS_SHIFT_GROUP 3
#define FFS_SHIFT_OTHER 0

#define FFS_DUMP_MODE_DIR 0700
#define FFS_DUMP_MODE_FILE 0666
#define FFS_DUMP_MODE_MKDIR 0755
#define FFS_DUMP_CTX_CAP 8

#define FFS_SQL_BEGIN "BEGIN IMMEDIATE"
#define FFS_SQL_COMMIT "COMMIT"
#define FFS_SQL_ROLLBACK "ROLLBACK"
#define FFS_SQL_SAVEPOINT "SAVEPOINT op"
#define FFS_SQL_RELEASE "RELEASE op"
#define FFS_SQL_SAVEPOINT_ROLLBACK "ROLLBACK TO op"

#define FFS_PERM_EXEC 1
#define FFS_PERM_WRITE 2
#define FFS_PERM_READ 4

#define SCHEMA_INODES                                                          \
    "CREATE TABLE IF NOT EXISTS inodes ("                                      \
    " ino INTEGER PRIMARY KEY AUTOINCREMENT,"                                  \
    " parent INTEGER NOT NULL,"                                                \
    " mode INTEGER NOT NULL,"                                                  \
    " uid INTEGER NOT NULL,"                                                   \
    " gid INTEGER NOT NULL,"                                                   \
    " nlink INTEGER NOT NULL,"                                                 \
    " size INTEGER NOT NULL,"                                                  \
    " atime INTEGER NOT NULL,"                                                 \
    " mtime INTEGER NOT NULL,"                                                 \
    " ctime INTEGER NOT NULL,"                                                 \
    " target TEXT);"                                                           \
    "CREATE TABLE IF NOT EXISTS file_extents ("                                \
    " ino INTEGER NOT NULL,"                                                   \
    " off INTEGER NOT NULL,"                                                   \
    " len INTEGER NOT NULL,"                                                   \
    " data BLOB NOT NULL,"                                                     \
    " PRIMARY KEY (ino, off),"                                                 \
    " CHECK(len BETWEEN 1 AND 65536)"                                          \
    ") WITHOUT ROWID;"

#define SCHEMA_DIRENTS                                                         \
    "CREATE TABLE IF NOT EXISTS dirents ("                                     \
    " parent INTEGER NOT NULL,"                                                \
    " name TEXT NOT NULL,"                                                     \
    " ino INTEGER NOT NULL,"                                                   \
    " PRIMARY KEY (parent, name));"                                            \
    "CREATE INDEX IF NOT EXISTS dirents_ino ON dirents(ino);"

struct ffs_of
{
    long ino;
    ffs_off offset;
    int flags;
    int is_dir;
    int refs;
    int used;
};

struct ffs_fd
{
    int of;
    int used;
};

struct ffs
{
    sqlite3 *db;
    long root;
    long cwd;
    long uid;
    long gid;
    unsigned long umask;
    int atime_on;
    int txn_depth;
    struct ffs_of ofs[FFS_MAX_FD];
    struct ffs_fd fds[FFS_MAX_FD];
};

struct ffs_dir
{
    ffs *fs;
    long ino;
    long count;
    long pos;
    char **names;
};

struct finode
{
    long ino;
    long parent;
    long mode;
    long uid;
    long gid;
    long nlink;
    ffs_off size;
    long atime;
    long mtime;
    long ctime;
    char *target;
};

/* db.c */
sqlite3_stmt *prepare(ffs *fs, const char *sql);
int exec_code(int rc);
const char *begin_sql(int nested);
const char *commit_sql(int nested);
int depth_down(int depth);
int root_code(int r);
void *choose_ptr(int on, void *on_true, void *on_false);
int begin(ffs *fs);
int commit(ffs *fs);
int rollback(ffs *fs);
long now(void);
char *xstrdup(const char *s);
int init_schema(ffs *fs);
ffs *ffs_alloc(sqlite3 *db);
long stat_count(ffs *fs, const char *sql);

/* path.c */
int path_missing(const char *path);
int too_long(size_t len);
int name_too_long(size_t n);
int word_count(int n, int r);
long choose_long(int on, long on_true, long on_false);
long alt_code(int r, const long *alt);
int up_more(ffs *fs, long ino);
int path_split(const char *path, char *buf, char **comps, int maxc,
               int *absolute, int *trailing);
int build_path(ffs *fs, long ino, char *buf, size_t bufsz);
int path_copy(const char *path, char *buf);
void path_marks(char *buf, size_t len, int *absolute, int *trailing);
char *scan_word(char *p, char **next);
int append_seg(char *path, const char *seg);
int seg_fits(size_t plen, size_t slen);
int path_into(char *buf, size_t bufsz, const char *path);

/* inode.c */
int q_code(long v, long err);
int kind_is_text(int kind);
int target_code(const char *p);
int no_refs(ffs *fs, long ino);
int trunc_dir_ok(const struct finode *fi);
int inode_mode(ffs *fs, long ino, long *mode);
int inode_parent(ffs *fs, long ino, long *parent);
int inode_target(ffs *fs, long ino, char **target);
int inode_load(ffs *fs, long ino, struct finode *fi);
void inode_free(struct finode *fi);
long inode_new(ffs *fs, const struct finode *fi);
int inode_store(ffs *fs, const struct finode *fi);
int inode_delete(ffs *fs, long ino);
int inode_meta(ffs *fs, long ino, long *mode, long *uid, long *gid);
int truncate_ino(ffs *fs, long ino, ffs_off len);
int drop_link(ffs *fs, long ino, long t);
int mode_is_dir(long mode);

/* extent.c */
int extent_len_bad(long len);
int extent_put_args_bad(ffs_off off, long len);
int extent_at(ffs *fs, long ino, ffs_off off, ffs_off *eoff,
              long *elen, unsigned char *buf);
int extent_next(ffs *fs, long ino, ffs_off off, ffs_off *eoff,
                long *elen, unsigned char *buf);
int extent_put(ffs *fs, long ino, ffs_off off, const unsigned char *buf,
               long len);
int extent_delete_overlap(ffs *fs, long ino, ffs_off start,
                          ffs_off end);
int extent_delete_from(ffs *fs, long ino, ffs_off start);
int extent_delete_all(ffs *fs, long ino);

/* data.c */
long zero_add(long n, unsigned char b);
long zero_count(const unsigned char *buf, long len);
int all_zero(const unsigned char *buf, long len);
long canonical_len(const unsigned char *buf, long len);
long min_long(long a, long b);
size_t min_size(size_t a, size_t b);
ffs_off min_off(ffs_off a, ffs_off b);
size_t hole_len(size_t hole, size_t room);
int read_end(int r, ffs_off eoff, ffs_off end);
int read_code(int r);
int page_code(int r);
long suf_blen(ffs_off end, ffs_off eoff, long elen);
ffs_off ext_end_of(ffs_off eoff, long elen, ffs_off end);
int straddle_of(ffs_off xoff, long elen, ffs_off new_size);
ssize_t data_read(ffs *fs, long ino, void *buf, size_t len, ffs_off off);
int data_write(ffs *fs, long ino, const void *buf, size_t len,
               ffs_off off);
int data_truncate(ffs *fs, long ino, ffs_off new_size);
int data_delete(ffs *fs, long ino);

/* dirent.c */
int insert_code(int rc);
long dirent_lookup(ffs *fs, long parent, const char *name);
int dirent_insert(ffs *fs, long parent, const char *name, long ino);
int dirent_delete(ffs *fs, long parent, const char *name);
int dir_touch(ffs *fs, long ino, long t);
int dir_nlink(ffs *fs, long ino, long delta);
int dir_count(ffs *fs, long ino);
char *dirent_name_of(ffs *fs, long parent, long ino);
int dirent_any(ffs *fs, long ino, long *parent, char **name);
int is_dir(ffs *fs, long ino);
long parent_of(ffs *fs, long ino);

/* resolve.c */
int is_ancestor(ffs *fs, long a, long b);
int comp_is_dot(const char *c);
int comp_is_dotdot(const char *c);
int follow_of(int follow_last, int follow_trailing, int trailing);
int symlink_mode(long mode);
long maybe_follow(ffs *fs, long ino, long dir, int do_follow, int hops,
                  long *parent_out, char *name_out);

struct rres
{
    long *parent;
    char *name;
    long *ino;
};

int resolve_from(ffs *fs, const char *path, long start, int hops,
                 int follow_last, int follow_trailing, struct rres r);
int resolve(ffs *fs, const char *path, int follow_last, long *parent,
            char *name, long *ino);
int fd_to_inode(ffs *fs, int dirfd, long *out);
int resolve_at(ffs *fs, int dirfd, const char *path, int follow_last,
               int follow_trailing, long *parent, char *name, long *ino);

/* perm.c */
struct perm_meta
{
    long mode;
    long uid;
    long gid;
};

typedef int (*perm_fn)(ffs *, long);

int fs_is_root(const ffs *fs);
int group_shift(const ffs *fs, long gid);
int shift_for(const ffs *fs, long uid, long gid);
int meta_code(int r);
int access_err(int ok);
int perm_denied(int ok);
int owner_match(const ffs *fs, const struct perm_meta *m);
int choose(int on, int on_true, int on_false);
int chain(int r, int next);
int perm_ok(const ffs *fs, long mode, long uid, long gid, int need);
int check_search(ffs *fs, long ino);
int check_read_file(ffs *fs, long ino);
int check_write_file(ffs *fs, long ino);
int check_create_dir(ffs *fs, long ino);
int check_list_dir(ffs *fs, long ino);
int check_owner(ffs *fs, long ino);
int check_times(ffs *fs, long ino);
int check_access(ffs *fs, long ino, int flags);
int access_ok(ffs *fs, long ino, int amode);
int root_ok(long mode, int need);
int need_read(int acc);
int need_write(int acc);

/* fdtable.c */
int of_slot_match(const ffs *fs, int i, long ino);
int of_taken(const ffs *fs, int i);
int slot_or_err(int i);
int fd_min(int min);
int fd_taken(const ffs *fs, int i);
int of_alloc(ffs *fs, long ino, int flags, int is_dir);
int fd_alloc2(ffs *fs, int of, int min);
int dup_min(ffs *fs, int fd, int min);
int ino_fd_refs(ffs *fs, long ino);

/* util.c */
int util_fail(int e);
int util_fail_r(int r);
int fail_code(int r);
int either_null(const void *a, const void *b);
int any_null3(const void *a, const void *b, const void *c);
int neg_code(long v);
int txn_run(ffs *fs, int (*op)(ffs *fs, void *ctx), void *ctx);
int fd_range_ok(int fd);
int fd_valid(const ffs *fs, int fd);
int fd_bad(const ffs *fs, int fd);
int fd_of(ffs *fs, int fd, int *of);
int done_result(int rc);
long q_i64(sqlite3_stmt *s, long err);
void sql_bind_i64(sqlite3_stmt *s, int i, long v);
void sql_bind_text(sqlite3_stmt *s, int i, const char *v);
int sql_row(sqlite3_stmt *s);
long sql_col_i64(sqlite3_stmt *s, int col);
void sql_fin(sqlite3_stmt *s);
int sql_done(sqlite3_stmt *s);

/* ops.c */
int fill_stat(ffs *fs, long ino, struct ffs_stat *st);
int find_code(int r);
int new_code(int r, const char *name);
int name_empty(const char *name);
int type_code(int odir, int ndir);
int guard_skip(int r, int flag);
int fd_pair_bad(const ffs *fs, int oldfd, int newfd);
int open_follow(int flags);
int open_excl_code(int create, int flags);
int open_retry(int r, int create);
int open_new_bad(int dirflag, const char *name, const char *path);
int open_type_code(int d, int dirflag);
int unlinked_unused(ffs *fs, long ino, long nlink);
int rw_read_code(int is_dir, int flags);
int rw_write_code(int is_dir, int flags);
int rw_trunc_code(int is_dir, int flags);
size_t read_want(const struct finode *fi, ffs_off pos, size_t n);
int write_end_bad(ffs_off pos, ffs_off d);
int seek_valid(int whence);

#endif
