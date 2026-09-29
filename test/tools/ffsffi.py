"""ctypes bindings for libfakefs (dev-only test driver)."""

import ctypes
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SONAME = os.environ.get("FAKEFS_SO", os.path.join(ROOT, "build", "libfakefs.so"))

c_ulong = ctypes.c_ulong
c_ssize_t = ctypes.c_ssize_t
c_off_t = ctypes.c_long


class FFSSstat(ctypes.Structure):
    _fields_ = [
        ("st_mode", c_ulong),
        ("st_nlink", c_ulong),
        ("st_uid", c_ulong),
        ("st_gid", c_ulong),
        ("st_size", c_ulong),
        ("st_ino", c_ulong),
        ("st_atime", c_off_t),
        ("st_mtime", c_off_t),
        ("st_ctime", c_off_t),
    ]


class FFSSstatfs(ctypes.Structure):
    _fields_ = [
        ("f_bsize", c_ulong),
        ("f_files", c_ulong),
        ("f_ffree", c_ulong),
        ("f_blocks", c_ulong),
        ("f_bfree", c_ulong),
    ]


# POSIX flag / mode constants (linux values)
O_RDONLY = 0
O_WRONLY = 1
O_RDWR = 2
O_CREAT = 0o100
O_EXCL = 0o200
O_TRUNC = 0o1000
O_APPEND = 0o2000
O_DIRECTORY = 0o200000
O_NOFOLLOW = 0o400000
AT_FDCWD = -100
AT_SYMLINK_NOFOLLOW = 0x100
AT_REMOVEDIR = 0x200

SEEK_SET = 0
SEEK_CUR = 1
SEEK_END = 2

S_IFMT = 0o170000
S_IFDIR = 0o040000
S_IFREG = 0o100000
S_IFLNK = 0o120000


def _is_dir(mode):
    return (mode & S_IFMT) == S_IFDIR


def _is_reg(mode):
    return (mode & S_IFMT) == S_IFREG


def _is_lnk(mode):
    return (mode & S_IFMT) == S_IFLNK


class FakeFS:
    """Wraps an ffs* handle with errno-aware helpers."""

    def __init__(self):
        self._lib = ctypes.CDLL(SONAME, use_errno=True)
        self._bind()
        self.fs = self._lib.ffs_create_memory()
        if not self.fs:
            raise RuntimeError("ffs_create_memory failed")

    def _bind(self):
        lib = self._lib
        for name, restype, argtypes in _SIGNATURES:
            fn = getattr(lib, name)
            fn.restype = restype
            fn.argtypes = argtypes

    def _errno(self):
        return ctypes.get_errno()

    def destroy(self):
        self._lib.ffs_destroy(self.fs)

    # --- filesystem lifecycle ---
    def mkdir(self, path, mode=0o755):
        r = self._lib.ffs_mkdir(self.fs, path.encode(), mode)
        return r == 0, self._errno()

    def rmdir(self, path):
        r = self._lib.ffs_rmdir(self.fs, path.encode())
        return r == 0, self._errno()

    def unlink(self, path):
        r = self._lib.ffs_unlink(self.fs, path.encode())
        return r == 0, self._errno()

    def link(self, old, new):
        r = self._lib.ffs_link(self.fs, old.encode(), new.encode())
        return r == 0, self._errno()

    def symlink(self, target, linkpath):
        r = self._lib.ffs_symlink(self.fs, target.encode(), linkpath.encode())
        return r == 0, self._errno()

    def readlink(self, path):
        buf = ctypes.create_string_buffer(4096)
        n = self._lib.ffs_readlink(self.fs, path.encode(), buf, 4096)
        if n < 0:
            return None, self._errno()
        return buf.raw[:n].decode(), 0

    def readlinkat(self, dirfd, path):
        buf = ctypes.create_string_buffer(4096)
        n = self._lib.ffs_readlinkat(self.fs, dirfd, path.encode(), buf, 4096)
        if n < 0:
            return None, self._errno()
        return buf.raw[:n].decode(), 0

    def rename(self, old, new):
        r = self._lib.ffs_rename(self.fs, old.encode(), new.encode())
        return r == 0, self._errno()

    def stat(self, path):
        st = FFSSstat()
        r = self._lib.ffs_stat(self.fs, path.encode(), ctypes.byref(st))
        if r != 0:
            return None, self._errno()
        return st, 0

    def lstat(self, path):
        st = FFSSstat()
        r = self._lib.ffs_lstat(self.fs, path.encode(), ctypes.byref(st))
        if r != 0:
            return None, self._errno()
        return st, 0

    def access(self, path, amode):
        r = self._lib.ffs_access(self.fs, path.encode(), amode)
        return r == 0, self._errno()

    def chmod(self, path, mode):
        r = self._lib.ffs_chmod(self.fs, path.encode(), mode)
        return r == 0, self._errno()

    def open(self, path, flags, mode=0o644):
        fd = self._lib.ffs_open(self.fs, path.encode(), flags, mode)
        if fd < 0:
            return None, self._errno()
        return fd, 0

    def close(self, fd):
        r = self._lib.ffs_close_fd(self.fs, fd)
        return r == 0, self._errno()

    def write(self, fd, data):
        buf = ctypes.create_string_buffer(data)
        n = self._lib.ffs_write(self.fs, fd, buf, len(data))
        return n, self._errno()

    def read(self, fd, size):
        buf = ctypes.create_string_buffer(size)
        n = self._lib.ffs_read(self.fs, fd, buf, size)
        if n < 0:
            return None, self._errno()
        return buf.raw[:n], 0

    def lseek(self, fd, off, whence):
        return self._lib.ffs_lseek(self.fs, fd, off, whence)

    def ftruncate(self, fd, length):
        r = self._lib.ffs_ftruncate(self.fs, fd, length)
        return r == 0, self._errno()

    def truncate(self, path, length):
        r = self._lib.ffs_truncate(self.fs, path.encode(), length)
        return r == 0, self._errno()

    def chdir(self, path):
        r = self._lib.ffs_chdir(self.fs, path.encode())
        return r == 0, self._errno()

    def getcwd(self):
        buf = ctypes.create_string_buffer(4096)
        r = self._lib.ffs_getcwd(self.fs, buf, 4096)
        if r != 0:
            return None, self._errno()
        return buf.value.decode(), 0

    def statfs(self):
        sb = FFSSstatfs()
        r = self._lib.ffs_statfs(self.fs, ctypes.byref(sb))
        if r != 0:
            return None, self._errno()
        return sb, 0

    def setids(self, uid, gid):
        r = self._lib.ffs_setids(self.fs, uid, gid)
        return r == 0, self._errno()

    def setumask(self, mask):
        r = self._lib.ffs_setumask(self.fs, mask)
        return r == 0, self._errno()

    def setatime(self, on):
        r = self._lib.ffs_setatime(self.fs, on)
        return r == 0, self._errno()

    def fstat(self, fd):
        st = FFSSstat()
        r = self._lib.ffs_fstat(self.fs, fd, ctypes.byref(st))
        if r != 0:
            return None, self._errno()
        return st, 0

    def fchmod(self, fd, mode):
        r = self._lib.ffs_fchmod(self.fs, fd, mode)
        return r == 0, self._errno()

    def fchown(self, fd, uid, gid):
        r = self._lib.ffs_fchown(self.fs, fd, uid, gid)
        return r == 0, self._errno()

    def fchdir(self, fd):
        r = self._lib.ffs_fchdir(self.fs, fd)
        return r == 0, self._errno()

    def dup(self, fd):
        n = self._lib.ffs_dup(self.fs, fd)
        if n < 0:
            return None, self._errno()
        return n, 0

    def dup2(self, oldfd, newfd):
        n = self._lib.ffs_dup2(self.fs, oldfd, newfd)
        if n < 0:
            return None, self._errno()
        return n, 0

    def pread(self, fd, size, off):
        buf = ctypes.create_string_buffer(size)
        n = self._lib.ffs_pread(self.fs, fd, buf, size, off)
        if n < 0:
            return None, self._errno()
        return buf.raw[:n], 0

    def pwrite(self, fd, data, off):
        buf = ctypes.create_string_buffer(data)
        n = self._lib.ffs_pwrite(self.fs, fd, buf, len(data), off)
        return n, self._errno()

    def fcntl(self, fd, cmd, arg=0):
        n = self._lib.ffs_fcntl(self.fs, fd, cmd, arg)
        if n < 0:
            return None, self._errno()
        return n, 0

    def realpath(self, path):
        buf = ctypes.create_string_buffer(4096)
        r = self._lib.ffs_realpath(self.fs, path.encode(), buf, 4096)
        if r != 0:
            return None, self._errno()
        return buf.value.decode(), 0

    def openat(self, dirfd, path, flags, mode=0o644):
        fd = self._lib.ffs_openat(self.fs, dirfd, path.encode(), flags, mode)
        if fd < 0:
            return None, self._errno()
        return fd, 0

    def mkdirat(self, dirfd, path, mode=0o755):
        r = self._lib.ffs_mkdirat(self.fs, dirfd, path.encode(), mode)
        return r == 0, self._errno()

    def unlinkat(self, dirfd, path, flags=0):
        r = self._lib.ffs_unlinkat(self.fs, dirfd, path.encode(), flags)
        return r == 0, self._errno()

    def linkat(self, olddirfd, oldpath, newdirfd, newpath, flags=0):
        r = self._lib.ffs_linkat(self.fs, olddirfd, oldpath.encode(),
                                 newdirfd, newpath.encode(), flags)
        return r == 0, self._errno()

    def symlinkat(self, target, newdirfd, linkpath):
        r = self._lib.ffs_symlinkat(self.fs, target.encode(),
                                    newdirfd, linkpath.encode())
        return r == 0, self._errno()

    def renameat(self, olddirfd, oldpath, newdirfd, newpath):
        r = self._lib.ffs_renameat(self.fs, olddirfd, oldpath.encode(),
                                   newdirfd, newpath.encode())
        return r == 0, self._errno()

    def fstatat(self, dirfd, path, flags=0):
        st = FFSSstat()
        r = self._lib.ffs_fstatat(self.fs, dirfd, path.encode(),
                                  ctypes.byref(st), flags)
        if r != 0:
            return None, self._errno()
        return st, 0

    def fchmodat(self, dirfd, path, mode, flags=0):
        r = self._lib.ffs_fchmodat(self.fs, dirfd, path.encode(), mode, flags)
        return r == 0, self._errno()

    def fchownat(self, dirfd, path, uid, gid, flags=0):
        r = self._lib.ffs_fchownat(self.fs, dirfd, path.encode(),
                                   uid, gid, flags)
        return r == 0, self._errno()

    def faccessat(self, dirfd, path, amode, flags=0):
        r = self._lib.ffs_faccessat(self.fs, dirfd, path.encode(),
                                    amode, flags)
        return r == 0, self._errno()

    def opendir(self, path):
        d = self._lib.ffs_opendir(self.fs, path.encode())
        if not d:
            return None, self._errno()
        names = []
        buf = ctypes.create_string_buffer(4096)
        while True:
            r = self._lib.ffs_readdir(d, buf, 4096)
            if r != 1:
                break
            names.append(buf.value.decode())
        self._lib.ffs_closedir(d)
        return names, 0

    def _walk(self):
        """Return a dict path -> ('dir'|'file'|'link', size/content-hash)."""
        result = {}

        def rec(path):
            st, err = self.lstat(path)
            if err:
                return
            if _is_dir(st.st_mode):
                result[path] = ("dir", None)
                names, _ = self.opendir(path)
                for n in names:
                    if n in (".", ".."):
                        continue
                    rec(path.rstrip("/") + "/" + n)
            elif _is_lnk(st.st_mode):
                tgt, _ = self.readlink(path)
                result[path] = ("link", tgt)
            else:
                fd, _ = self.open(path, O_RDONLY)
                if fd is None:
                    result[path] = ("file", "")
                    return
                data, _ = self.read(fd, 1 << 20)
                self.close(fd)
                result[path] = ("file", bytes(data))

        rec("/")
        return result


# (name, restype, argtypes)
_SIGNATURES = [
    ("ffs_create", ctypes.c_void_p, [ctypes.c_char_p]),
    ("ffs_create_memory", ctypes.c_void_p, []),
    ("ffs_destroy", None, [ctypes.c_void_p]),
    ("ffs_setatime", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int]),
    ("ffs_setids", ctypes.c_int, [ctypes.c_void_p, c_ulong, c_ulong]),
    ("ffs_setumask", ctypes.c_int, [ctypes.c_void_p, c_ulong]),
    ("ffs_mkdir", ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p, c_ulong]),
    ("ffs_rmdir", ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p]),
    ("ffs_unlink", ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p]),
    ("ffs_link", ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]),
    ("ffs_symlink", ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]),
    ("ffs_readlink", c_ssize_t, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]),
    ("ffs_rename", ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]),
    ("ffs_stat", ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(FFSSstat)]),
    ("ffs_lstat", ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(FFSSstat)]),
    ("ffs_access", ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]),
    ("ffs_chmod", ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p, c_ulong]),
    ("ffs_chdir", ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p]),
    ("ffs_getcwd", ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]),
    ("ffs_realpath", ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]),
    ("ffs_statfs", ctypes.c_int, [ctypes.c_void_p, ctypes.POINTER(FFSSstatfs)]),
    ("ffs_opendir", ctypes.c_void_p, [ctypes.c_void_p, ctypes.c_char_p]),
    ("ffs_readdir", ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]),
    ("ffs_closedir", ctypes.c_int, [ctypes.c_void_p]),
    ("ffs_open", ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, c_ulong]),
    ("ffs_close_fd", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int]),
    ("ffs_read", c_ssize_t, [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t]),
    ("ffs_write", c_ssize_t, [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t]),
    ("ffs_lseek", c_off_t, [ctypes.c_void_p, ctypes.c_int, c_off_t, ctypes.c_int]),
    ("ffs_ftruncate", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int, c_off_t]),
    ("ffs_truncate", ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p, c_off_t]),
    ("ffs_fstat", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(FFSSstat)]),
    ("ffs_dup", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int]),
    ("ffs_dup2", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]),
    ("ffs_pread", c_ssize_t, [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t, c_off_t]),
    ("ffs_pwrite", c_ssize_t, [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t, c_off_t]),
    ("ffs_fcntl", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_long]),
    ("ffs_setumask", ctypes.c_int, [ctypes.c_void_p, c_ulong]),
    ("ffs_fstat", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(FFSSstat)]),
    ("ffs_fchmod", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int, c_ulong]),
    ("ffs_fchown", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int, c_ulong, c_ulong]),
    ("ffs_fchdir", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int]),
    ("ffs_openat", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int, c_ulong]),
    ("ffs_mkdirat", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, c_ulong]),
    ("ffs_unlinkat", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]),
    ("ffs_linkat", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]),
    ("ffs_symlinkat", ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p]),
    ("ffs_renameat", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p]),
    ("ffs_fstatat", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.POINTER(FFSSstat), ctypes.c_int]),
    ("ffs_fchmodat", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, c_ulong, ctypes.c_int]),
    ("ffs_fchownat", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, c_ulong, c_ulong, ctypes.c_int]),
    ("ffs_faccessat", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_int]),
    ("ffs_readlinkat", c_ssize_t, [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]),
]
