#ifndef FAKEFS_H
#define FAKEFS_H

#define FAKEFS_VERSION_MAJOR 0
#define FAKEFS_VERSION_MINOR 2
#define FAKEFS_VERSION_PATCH 0

#include <stddef.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>

typedef struct ffs ffs;
typedef struct ffs_dir ffs_dir;

/* Flags not exposed by strict C89 <fcntl.h>. */
#ifndef O_DIRECTORY
#define O_DIRECTORY 0200000
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0400000
#endif
#ifndef F_DUPFD
#define F_DUPFD 0
#endif
#ifndef F_GETFL
#define F_GETFL 3
#endif
#ifndef F_SETFL
#define F_SETFL 4
#endif

/* Directory-relative (*at) flags not exposed by strict C89 <fcntl.h>. */
#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif
#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif
#ifndef AT_REMOVEDIR
#define AT_REMOVEDIR 0x200
#endif
#ifndef AT_SYMLINK_FOLLOW
#define AT_SYMLINK_FOLLOW 0x400
#endif
#ifndef AT_EACCESS
#define AT_EACCESS 0x200
#endif

/* access(2) mode bits. */
#ifndef F_OK
#define F_OK 0
#endif
#ifndef X_OK
#define X_OK 1
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef R_OK
#define R_OK 4
#endif

/* POSIX mode bits (same values as <sys/stat.h>). */
#define FFS_S_IFMT 0170000UL
#define FFS_S_IFDIR 0040000UL
#define FFS_S_IFREG 0100000UL
#define FFS_S_IFLNK 0120000UL

struct ffs_stat
{
    unsigned long st_mode;
    unsigned long st_nlink;
    unsigned long st_uid;
    unsigned long st_gid;
    unsigned long st_size;
    unsigned long st_ino;
    time_t st_atime;
    time_t st_mtime;
    time_t st_ctime;
};

struct ffs_statfs
{
    unsigned long f_bsize;
    unsigned long f_files;
    unsigned long f_ffree;
    unsigned long f_blocks;
    unsigned long f_bfree;
};

ffs *ffs_create(const char *path);
ffs *ffs_create_memory(void);
void ffs_destroy(ffs *fs);

int ffs_setatime(ffs *fs, int on);
int ffs_setids(ffs *fs, unsigned long uid, unsigned long gid);
int ffs_setumask(ffs *fs, unsigned long mask);

int ffs_mkdir(ffs *fs, const char *path, unsigned long mode);
int ffs_rmdir(ffs *fs, const char *path);
int ffs_unlink(ffs *fs, const char *path);
int ffs_link(ffs *fs, const char *oldpath, const char *newpath);
int ffs_symlink(ffs *fs, const char *target, const char *linkpath);
ssize_t ffs_readlink(ffs *fs, const char *path, char *buf, size_t bufsz);
int ffs_rename(ffs *fs, const char *oldpath, const char *newpath);
int ffs_stat(ffs *fs, const char *path, struct ffs_stat *st);
int ffs_lstat(ffs *fs, const char *path, struct ffs_stat *st);
int ffs_chmod(ffs *fs, const char *path, unsigned long mode);
int ffs_chown(ffs *fs, const char *path, unsigned long uid,
              unsigned long gid);
int ffs_lchown(ffs *fs, const char *path, unsigned long uid,
               unsigned long gid);
int ffs_utimes(ffs *fs, const char *path, const struct timeval tv[2]);
int ffs_truncate(ffs *fs, const char *path, off_t len);
int ffs_access(ffs *fs, const char *path, int amode);

int ffs_openat(ffs *fs, int dirfd, const char *path, int flags,
               unsigned long mode);
int ffs_mkdirat(ffs *fs, int dirfd, const char *path, unsigned long mode);
int ffs_unlinkat(ffs *fs, int dirfd, const char *path, int flags);
int ffs_linkat(ffs *fs, int olddirfd, const char *oldpath, int newdirfd,
               const char *newpath, int flags);
int ffs_symlinkat(ffs *fs, const char *target, int newdirfd,
                  const char *linkpath);
ssize_t ffs_readlinkat(ffs *fs, int dirfd, const char *path, char *buf,
                       size_t bufsz);
int ffs_renameat(ffs *fs, int olddirfd, const char *oldpath, int newdirfd,
                 const char *newpath);
int ffs_fstatat(ffs *fs, int dirfd, const char *path, struct ffs_stat *st,
                int flags);
int ffs_fchmodat(ffs *fs, int dirfd, const char *path, unsigned long mode,
                 int flags);
int ffs_fchownat(ffs *fs, int dirfd, const char *path, unsigned long uid,
                 unsigned long gid, int flags);
int ffs_faccessat(ffs *fs, int dirfd, const char *path, int amode, int flags);

int ffs_chdir(ffs *fs, const char *path);
int ffs_getcwd(ffs *fs, char *buf, size_t bufsz);
int ffs_realpath(ffs *fs, const char *path, char *buf, size_t bufsz);

int ffs_statfs(ffs *fs, struct ffs_statfs *buf);

ffs_dir *ffs_opendir(ffs *fs, const char *path);
int ffs_readdir(ffs_dir *d, char *name, size_t namesz);
int ffs_closedir(ffs_dir *d);

int ffs_open(ffs *fs, const char *path, int flags, unsigned long mode);
int ffs_close_fd(ffs *fs, int fd);
ssize_t ffs_read(ffs *fs, int fd, void *buf, size_t n);
ssize_t ffs_write(ffs *fs, int fd, const void *buf, size_t n);
off_t ffs_lseek(ffs *fs, int fd, off_t off, int whence);
int ffs_ftruncate(ffs *fs, int fd, off_t len);
int ffs_fstat(ffs *fs, int fd, struct ffs_stat *st);
int ffs_fchmod(ffs *fs, int fd, unsigned long mode);
int ffs_fchown(ffs *fs, int fd, unsigned long uid, unsigned long gid);
int ffs_fchdir(ffs *fs, int fd);
int ffs_dup(ffs *fs, int fd);
int ffs_dup2(ffs *fs, int oldfd, int newfd);
ssize_t ffs_pread(ffs *fs, int fd, void *buf, size_t n, off_t off);
ssize_t ffs_pwrite(ffs *fs, int fd, const void *buf, size_t n, off_t off);
int ffs_fcntl(ffs *fs, int fd, int cmd, long arg);

#endif
