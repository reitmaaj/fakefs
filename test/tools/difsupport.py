"""Shared helpers for differential tests that replay POSIX operations
against a real filesystem (reference) and a :memory: libfakefs volume
(subject), asserting success/errno and namespace parity."""

import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ffsffi import O_CREAT, O_WRONLY, O_TRUNC, O_RDONLY, O_APPEND  # noqa: E402

# The op vocabulary shared by the reference (real FS) and subject (fake).
# Each op is a tuple whose first element names the operation; success is a
# boolean; on failure the reported errno is compared between the two sides.
_APPLY_OK = (True, 0)

S_IFMT = 0o170000
S_IFDIR = 0o040000
S_IFLNK = 0o120000


def real_path(tmp, path):
    return os.path.join(tmp, path.lstrip("/"))


def apply_fake(fs, op):
    """Apply an op to a :memory: fakefs; return (ok, errno)."""
    k = op[0]
    if k == "mkdir":
        return fs.mkdir(op[1])
    if k == "rmdir":
        return fs.rmdir(op[1])
    if k == "unlink":
        return fs.unlink(op[1])
    if k == "link":
        return fs.link(op[1], op[2])
    if k == "symlink":
        return fs.symlink(op[1], op[2])
    if k == "rename":
        return fs.rename(op[1], op[2])
    if k == "truncate":
        return fs.truncate(op[1], op[2])
    if k == "chmod":
        return fs.chmod(op[1], op[2])
    if k == "create":
        fd, e = fs.open(op[1], O_CREAT | O_WRONLY | O_TRUNC, 0o644)
        if fd is None:
            return False, e
        fs.write(fd, op[2])
        fs.close(fd)
        return _APPLY_OK
    if k == "overwrite":
        fd, e = fs.open(op[1], O_WRONLY | O_TRUNC)
        if fd is None:
            return False, e
        fs.write(fd, op[2])
        fs.close(fd)
        return _APPLY_OK
    if k == "append":
        fd, e = fs.open(op[1], O_WRONLY | O_APPEND)
        if fd is None:
            return False, e
        fs.write(fd, op[2])
        fs.close(fd)
        return _APPLY_OK
    if k == "read":
        fd, e = fs.open(op[1], O_RDONLY)
        if fd is None:
            return False, e
        data, _ = fs.read(fd, 1 << 20)
        fs.close(fd)
        return data == op[2], 0
    raise ValueError(op[0])


def apply_real(tmp, op):
    """Apply an op to the real reference tree; return (ok, errno)."""
    k = op[0]
    try:
        if k == "mkdir":
            os.mkdir(real_path(tmp, op[1]))
        elif k == "rmdir":
            os.rmdir(real_path(tmp, op[1]))
        elif k == "unlink":
            os.unlink(real_path(tmp, op[1]))
        elif k == "link":
            os.link(real_path(tmp, op[1]), real_path(tmp, op[2]))
        elif k == "symlink":
            os.symlink(op[1], real_path(tmp, op[2]))
        elif k == "rename":
            os.rename(real_path(tmp, op[1]), real_path(tmp, op[2]))
        elif k == "truncate":
            os.truncate(real_path(tmp, op[1]), op[2])
        elif k == "chmod":
            os.chmod(real_path(tmp, op[1]), op[2])
        elif k == "create":
            with open(real_path(tmp, op[1]), "wb") as fh:
                fh.write(op[2])
        elif k == "overwrite":
            with open(real_path(tmp, op[1]), "wb") as fh:
                fh.write(op[2])
        elif k == "append":
            with open(real_path(tmp, op[1]), "ab") as fh:
                fh.write(op[2])
        elif k == "read":
            with open(real_path(tmp, op[1]), "rb") as fh:
                return fh.read() == op[2], 0
        else:
            raise ValueError(k)
        return _APPLY_OK
    except OSError as exc:
        return False, exc.errno


def tree(tmp):
    """Normalized snapshot of a real directory: path -> (type, target|bytes)."""
    out = {}

    def walk(rel):
        key = "/" if rel == "" else "/" + rel + "/"
        out[key] = ("dir", None)
        full = os.path.join(tmp, rel)
        for name in sorted(os.listdir(full)):
            child = os.path.join(full, name)
            crel = name if rel == "" else rel + "/" + name
            if os.path.isdir(child) and not os.path.islink(child):
                walk(crel)
            elif os.path.islink(child):
                out["/" + crel] = ("link", os.readlink(child))
            else:
                with open(child, "rb") as fh:
                    out["/" + crel] = ("file", fh.read())

    walk("")
    return out


def fake_tree(fs):
    """Normalized snapshot of a fakefs volume: path -> (type, target|bytes)."""
    out = {}

    def walk(path):
        st, e = fs.lstat(path)
        if e:
            return
        mode = st.st_mode & S_IFMT
        if mode == S_IFDIR:
            key = "/" if path == "/" else path + "/"
            out[key] = ("dir", None)
            names, _ = fs.opendir(path)
            for n in names:
                if n in (".", ".."):
                    continue
                walk(path.rstrip("/") + "/" + n)
        elif mode == S_IFLNK:
            tgt, _ = fs.readlink(path)
            out[path] = ("link", tgt)
        else:
            fd, _ = fs.open(path, O_RDONLY)
            data, _ = fs.read(fd, 1 << 20)
            fs.close(fd)
            out[path] = ("file", bytes(data))

    walk("/")
    return out


def is_tmpfs(path):
    """True when `path` lives on a tmpfs mount (linux)."""
    try:
        target = os.path.realpath(path) + "/"
        with open("/proc/mounts", "r") as fh:
            for line in fh:
                parts = line.split()
                if len(parts) < 2 or parts[2] != "tmpfs":
                    continue
                mnt = parts[1].replace("\\040", " ")
                mnt = mnt if mnt.endswith("/") else mnt + "/"
                if target.startswith(mnt):
                    return True
    except OSError:
        return False
    return False


def find_tmpfs_root():
    """Return a writable tmpfs directory to host the reference tree, or None."""
    for candidate in ("/dev/shm", "/tmp", "/run"):
        if not os.path.isdir(candidate):
            continue
        if not os.access(candidate, os.W_OK):
            continue
        if is_tmpfs(candidate):
            return candidate
    return None


def make_tmpfs_tree():
    """Create a fresh reference root on tmpfs; caller removes it."""
    root = find_tmpfs_root()
    if root is None:
        return None
    return tempfile.mkdtemp(prefix="fakefs_tmpfs_", dir=root)


def rmtree(path):
    if path is not None:
        shutil.rmtree(path, ignore_errors=True)
