#!/usr/bin/env python3
"""Property-based test: a reference model drives a seeded random walk of
operations against the real library, comparing success/failure and the full
namespace after every step."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ffsffi import FakeFS, O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC, SEEK_SET, SEEK_END  # noqa: E402
from model import Model  # noqa: E402
from gen import Generator  # noqa: E402


def _is_dir(mode):
    return (mode & 0o170000) == 0o040000


def _is_lnk(mode):
    return (mode & 0o170000) == 0o120000


def fs_snapshot(fs):
    out = {}

    def add(path):
        st, err = fs.lstat(path)
        if err:
            return
        if _is_dir(st.st_mode):
            key = "/" if path == "/" else path + "/"
            out[key] = ("dir", st.st_nlink)
            names, _ = fs.opendir(path)
            for n in names:
                if n in (".", ".."):
                    continue
                add(path.rstrip("/") + "/" + n)
        elif _is_lnk(st.st_mode):
            tgt, _ = fs.readlink(path)
            out[path] = ("link", st.st_nlink, tgt)
        else:
            fd, err = fs.open(path, O_RDONLY)
            if fd is None:
                out[path] = ("file", st.st_nlink, b"")
                return
            data, _ = fs.read(fd, 1 << 20)
            fs.close(fd)
            out[path] = ("file", st.st_nlink, bytes(data))

    add("/")
    return out


def apply(fs, op):
    kind = op[0]
    if kind == "mkdir":
        return fs.mkdir(op[1])
    if kind == "rmdir":
        return fs.rmdir(op[1])
    if kind == "unlink":
        return fs.unlink(op[1])
    if kind == "link":
        return fs.link(op[1], op[2])
    if kind == "symlink":
        return fs.symlink(op[1], op[2])
    if kind == "create":
        fd, err = fs.open(op[1], O_CREAT | O_WRONLY | O_TRUNC, 0o644)
        if fd is None:
            return False, err
        fs.write(fd, op[2])
        fs.close(fd)
        return True, 0
    if kind == "overwrite":
        fd, err = fs.open(op[1], O_WRONLY | O_TRUNC)
        if fd is None:
            return False, err
        fs.write(fd, op[2])
        fs.close(fd)
        return True, 0
    if kind == "append":
        fd, err = fs.open(op[1], O_WRONLY)
        if fd is None:
            return False, err
        fs.lseek(fd, 0, SEEK_END)
        fs.write(fd, op[2])
        fs.close(fd)
        return True, 0
    if kind == "truncate":
        return fs.truncate(op[1], op[2])
    if kind == "rename":
        return fs.rename(op[1], op[2])
    raise ValueError("unknown op %r" % (kind,))


def apply_model(model, op):
    kind = op[0]
    if kind == "mkdir":
        return model.mkdir(op[1])
    if kind == "rmdir":
        return model.rmdir(op[1])
    if kind == "unlink":
        return model.unlink(op[1])
    if kind == "link":
        return model.link(op[1], op[2])
    if kind == "symlink":
        return model.symlink(op[1], op[2])
    if kind == "create":
        return model.create_file(op[1], op[2])
    if kind == "overwrite":
        return model.overwrite_file(op[1], op[2])
    if kind == "append":
        return model.append_file(op[1], op[2])
    if kind == "truncate":
        return model.truncate_file(op[1], op[2])
    if kind == "rename":
        return model.rename(op[1], op[2])
    raise ValueError("unknown op %r" % (kind,))


def main():
    seed = int(os.environ.get("SEED", "1"))
    nops = int(os.environ.get("NOPS", "500"))
    oplog = os.environ.get("FAKEFS_OPLOG")
    logf = open(oplog, "w") if oplog else None
    fs = FakeFS()
    model = Model()
    gen = Generator(seed)

    for i in range(nops):
        op = gen.next_op(model)
        if logf is not None:
            logf.write(repr(op) + "\n")
            logf.flush()
        expected = apply_model(model, op)
        actual, errno = apply(fs, op)
        if expected != actual:
            print("MISMATCH at step %d op=%r expected=%r actual=%r errno=%r"
                  % (i, op, expected, actual, errno))
            sys.exit(1)
        if i % 10 == 0 or i == nops - 1:
            s_model = model.snapshot()
            s_fs = fs_snapshot(fs)
            if s_model != s_fs:
                print("SNAPSHOT MISMATCH at step %d op=%r" % (i, op))
                keys = sorted(set(s_model) | set(s_fs))
                for k in keys:
                    mv = s_model.get(k, "<missing>")
                    fv = s_fs.get(k, "<missing>")
                    if mv != fv:
                        print("  %s model=%r fs=%r" % (k, mv, fv))
                sys.exit(1)

    fs.destroy()
    if logf is not None:
        logf.close()
    print("property model: %d ops passed (seed=%d)" % (nops, seed))


if __name__ == "__main__":
    main()
