"""Thin libc wrappers for directory-relative (*at) syscalls on the real side.

Python 3.13+ removed the os.*at convenience functions, so the differential
runner reaches the kernel through libc directly. Every wrapper returns
(ok: bool, errno: int) for boolean ops, or (value, errno) for ops that return
a descriptor/byte count. errno is read via ctypes errno, which libc sets on
the calling thread."""

import ctypes

_LIBC = ctypes.CDLL(None, use_errno=True)

AT_FDCWD = -100
AT_SYMLINK_NOFOLLOW = 0x100
AT_REMOVEDIR = 0x200
AT_SYMLINK_FOLLOW = 0x400
AT_EACCESS = 0x200


def _err():
    return ctypes.get_errno()


def _bind(name, restype, argtypes):
    fn = getattr(_LIBC, name)
    fn.restype = restype
    fn.argtypes = argtypes
    return fn


_openat = _bind("openat", ctypes.c_int,
                [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_uint])
_mkdirat = _bind("mkdirat", ctypes.c_int,
                 [ctypes.c_int, ctypes.c_char_p, ctypes.c_uint])
_unlinkat = _bind("unlinkat", ctypes.c_int,
                  [ctypes.c_int, ctypes.c_char_p, ctypes.c_int])
_renameat = _bind("renameat", ctypes.c_int,
                  [ctypes.c_int, ctypes.c_char_p,
                   ctypes.c_int, ctypes.c_char_p])
_linkat = _bind("linkat", ctypes.c_int,
                [ctypes.c_int, ctypes.c_char_p,
                 ctypes.c_int, ctypes.c_char_p, ctypes.c_int])
_symlinkat = _bind("symlinkat", ctypes.c_int,
                   [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p])
_readlinkat = _bind("readlinkat", ctypes.c_ssize_t,
                    [ctypes.c_int, ctypes.c_char_p,
                     ctypes.c_char_p, ctypes.c_size_t])
_fchmodat = _bind("fchmodat", ctypes.c_int,
                  [ctypes.c_int, ctypes.c_char_p, ctypes.c_uint, ctypes.c_int])
_faccessat = _bind("faccessat", ctypes.c_int,
                   [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_int])
_close = _bind("close", ctypes.c_int, [ctypes.c_int])


def openat(dirfd, path, flags, mode=0):
    fd = _openat(dirfd, path.encode(), flags, mode)
    if fd < 0:
        return None, _err()
    return fd, 0


def mkdirat(dirfd, path, mode=0o777):
    r = _mkdirat(dirfd, path.encode(), mode)
    return r == 0, (0 if r == 0 else _err())


def unlinkat(dirfd, path, flags=0):
    r = _unlinkat(dirfd, path.encode(), flags)
    return r == 0, (0 if r == 0 else _err())


def renameat(odir, old, ndir, new):
    r = _renameat(odir, old.encode(), ndir, new.encode())
    return r == 0, (0 if r == 0 else _err())


def linkat(odir, old, ndir, new, flags=0):
    r = _linkat(odir, old.encode(), ndir, new.encode(), flags)
    return r == 0, (0 if r == 0 else _err())


def symlinkat(target, newdir, linkpath):
    r = _symlinkat(target.encode(), newdir, linkpath.encode())
    return r == 0, (0 if r == 0 else _err())


def readlinkat(dirfd, path, size=4096):
    buf = ctypes.create_string_buffer(size)
    n = _readlinkat(dirfd, path.encode(), buf, size)
    if n < 0:
        return None, _err()
    return buf.raw[:n], 0


def fchmodat(dirfd, path, mode, flags=0):
    r = _fchmodat(dirfd, path.encode(), mode, flags)
    return r == 0, (0 if r == 0 else _err())


def faccessat(dirfd, path, amode, flags=0):
    r = _faccessat(dirfd, path.encode(), amode, flags)
    return r == 0, (0 if r == 0 else _err())


def close(fd):
    return _close(fd)
