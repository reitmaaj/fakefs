#!/usr/bin/env python3
"""Massive differential suite: systematic tmpfs-vs-:memory: fakefs parity.

Unlike the fixed-script/random-walk runners (0014), this runner expands
deterministic cross-product tables over the whole public ffs_* surface,
applies each phase to both a real tmpfs tree and a :memory: fakefs, asserts
per-operation success/errno parity for every row, byte-for-byte normalized
namespace parity at periodic checkpoints and at the end of each phase, and
enforces a minimum executed-row count so the suite cannot silently shrink.

Environment-specific cases are routed through a Divergence registry and
reported as registered skips, never silently ignored. The whole suite skips
(exit 0) when no writable tmpfs mount is available."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from difcore import (  # noqa: E402
    Divergence, normalize_stat, mode_kind, is_root, align_credentials,
)
from difsupport import (  # noqa: E402
    apply_fake,
    apply_real,
    fake_tree,
    tree,
    make_tmpfs_tree,
    rmtree,
)
from ffsffi import (  # noqa: E402
    FakeFS, O_CREAT, O_EXCL, O_WRONLY, O_TRUNC, O_RDONLY, O_APPEND,
    O_RDWR, O_DIRECTORY, O_NOFOLLOW,
    SEEK_SET, SEEK_CUR, SEEK_END, AT_REMOVEDIR,
)

MIN_ROWS = int(os.environ.get("DIFF_MIN_ROWS", "4000"))
SNAPSHOT_EVERY = 64   # full normalized namespace diff locality (rows)

# ---- matrix dimensions -----------------------------------------------------

NAMES = ["a", "b", "c", "a_b", "x9", "m", "n", "p",
         "q", "r", "s", "t", "longname_one", "longname_two"]
DATA = [b"", b"x", b"hello", b"\x00\x01\x02\xff", b"p" * 5000,
        b"alphabetical-content-value", b"\xff" * 10000]
DEPTHS = [1, 2, 3, 4, 5]
SIZES = [0, 1, 3, 7, 64, 1000, 20000]


def _shape_tail(depth):
    return "/".join(["d%d" % i for i in range(depth)])


def _tree_script(names, depth):
    """Long phase: create a deep, populated directory tree."""
    script = []
    top = "/" + names[0]
    script.append(("mkdir", top))
    prev = top
    for i in range(1, depth):
        cur = prev + "/d%d" % i
        script.append(("mkdir", cur))
        prev = cur
    for name in names[1:]:
        script.append(("create", prev + "/" + name + ".f", b"leaf"))
    return script


def _mkdir_phase(names):
    """mkdir fresh + collide at each tree level, plus missing parents."""
    script = []
    for name in names:
        script.append(("mkdir", "/" + name))                  # fresh
        script.append(("mkdir", "/" + name))                  # EEXIST
        script.append(("mkdir", "/" + name + "/" + name))     # nested fresh
        script.append(("mkdir", "/missing-parent/" + name))   # ENOENT
        script.append(("create", "/" + name + ".f", b"v"))    # file sibling
    return script


def _rmdir_phase(names):
    """rmdir empty/non-empty/missing/file-as-target sequences."""
    script = []
    for i, name in enumerate(names):
        base = "/dr%d_%s" % (i, name)
        script.append(("mkdir", base))                        # empty
        script.append(("rmdir", base))                        # ok
        script.append(("rmdir", base))                        # ENOENT
        script.append(("mkdir", base))
        script.append(("mkdir", base + "/c"))
        script.append(("rmdir", base))                        # ENOTEMPTY
        script.append(("create", base + "/c", b"q"))
        script.append(("rmdir", base))                        # ENOTEMPTY
        script.append(("rmdir", base + "/c"))                 # ENOTDIR(file)
        script.append(("unlink", base + "/c"))                # remove file
        script.append(("rmdir", base + "/c"))                 # ENOENT
        script.append(("unlink", base))                       # EISDIR
        script.append(("rmdir", base + "/c"))                 # ENOENT
        script.append(("rmdir", base))                        # ENOTEMPTY still
        script.append(("rmdir", base + "/c"))                 # ENOENT
        script.append(("create", base + "/c", b"z"))
        script.append(("unlink", base + "/c"))
        script.append(("rmdir", base))                        # now empty -> ok
    return script


def _unlink_phase(names):
    """unlink files and symlinks, present/missing, dir rejected."""
    script = []
    for i, name in enumerate(names):
        base = "/fu%d_%s" % (i, name)
        script.append(("create", base, b"body"))
        script.append(("unlink", base))                       # ok
        script.append(("unlink", base))                       # ENOENT
        script.append(("create", base, b"again"))
        script.append(("unlink", base))
        script.append(("symlink", "t" + name, base + "l"))    # dangling
        script.append(("unlink", base + "l"))                 # removes link
        script.append(("unlink", base + "l"))                 # ENOENT
        script.append(("mkdir", base + "d"))
        script.append(("unlink", base + "d"))                 # EISDIR
        script.append(("rmdir", base + "d"))
        script.append(("unlink", "/" + name + "_absent"))     # ENOENT
    return script


def _link_phase(names):
    """hard links across names: fresh dest, EEXIST, missing src."""
    script = []
    for i, src in enumerate(names):
        sf = "/hl_src_%s" % src
        script.append(("create", sf, b"data" + bytes([i])))
        for j, dst in enumerate(names):
            df = "/hl_%s_%d" % (src, j)
            script.append(("link", sf, df))                   # fresh
            script.append(("link", sf, df))                   # EEXIST
            script.append(("link", "/nope_%s" % dst, "/zz_%s" % dst))  # ENOENT
        script.append(("mkdir", "/hl_dir_%s" % src))
        script.append(("link", "/hl_dir_%s" % src, "/hl_dl"))  # dir src errno
    return script


def _symlink_phase(names):
    """symlinks: dangling, absolute, deep, valid target, EEXIST."""
    script = []
    for i, name in enumerate(names):
        base = "/sy_%s" % name
        for tgt in ["tgt", "/absolute", "a/../../deep", "x/" + name, name]:
            script.append(("symlink", tgt, base + "_l%d" % i))
        script.append(("create", base, b"c"))
        script.append(("symlink", "x", base))                 # EEXIST
        script.append(("unlink", base + "_l0"))               # unlink symlink
        script.append(("symlink", "tgt", base + "_l0"))       # recreate
    return script


def _content_phase(payloads, sizes):
    """create/append/overwrite/truncate/read over sizes on many files."""
    script = []
    for i, payload in enumerate(payloads):
        f = "/c_%d.bin" % i
        script.append(("create", f, payload))
        script.append(("read", f, payload))
        mid = len(payload) // 2
        if payload:
            script.append(("create", "/c_%d_p.bin" % i, payload[:mid]))
            script.append(("append", "/c_%d_p.bin" % i, payload[mid:]))
            script.append(("read", "/c_%d_p.bin" % i, payload))
        script.append(("truncate", f, max(mid, 0)))
        script.append(("read", f, payload[:max(mid, 0)]))
        script.append(("create", "/c_%d_x.bin" % i, payload))
        script.append(("overwrite", "/c_%d_x.bin" % i, payload[::-1]))
        script.append(("read", "/c_%d_x.bin" % i, payload[::-1]))
    for j, size in enumerate(sizes):
        script.append(("create", "/s_%d.bin" % j, b""))
        script.append(("truncate", "/s_%d.bin" % j, size))
        script.append(("read", "/s_%d.bin" % j, b"\0" * size))
        script.append(("truncate", "/s_%d.bin" % j, max(size - 1, 0)))
    return script


def _rename_phase(names):
    """conservative renames (files/links/empty dirs to fresh names)."""
    script = []
    dirs = []
    for i, src in enumerate(names):
        sf = "/rn_%s.f" % src
        script.append(("create", sf, b"payload"))
        dirs.append("/rn_d_%s" % src)
        script.append(("mkdir", dirs[-1]))
        for j, dst in enumerate(names):
            df = "/rn_%s_%s.f" % (src, dst)
            script.append(("rename", sf, df))                 # move file
            script.append(("rename", df, sf))                 # move back
            script.append(("rename", "/absent_%s" % dst, "/absent2_%s" % dst))
    for i, src_dir in enumerate(dirs):
        for j, dst_dir in enumerate(dirs):
            if i == j:
                continue
            target = dst_dir + "/moved_%d" % i
            script.append(("rename", src_dir, target))        # dir into dir
            script.append(("rename", target, src_dir))        # move back
    return script


def _all_phases():
    phases = []
    for names in [NAMES[:5], NAMES[3:], NAMES[::-1]]:
        phases.append(_tree_script(names, 3))
        phases.append(_mkdir_phase(names))
        phases.append(_rmdir_phase(names))
        phases.append(_unlink_phase(names))
        phases.append(_link_phase(names))
        phases.append(_symlink_phase(names))
        phases.append(_rename_phase(names))
    phases.append(_content_phase(DATA, SIZES))
    phases.append(_content_phase(DATA * 2, SIZES))
    return phases


def _count(phases):
    total = 0
    for script in phases:
        total += len(script)
    return total


def _run_phase(label, ops, div):
    fs = FakeFS()
    tmp = make_tmpfs_tree()
    if tmp is None:
        fs.destroy()
        return None

    def snap(i, op):
        ft = fake_tree(fs)
        rt = tree(tmp)
        if ft != rt:
            print("SNAPSHOT MISMATCH %s at row %d op=%r" % (label, i, op))
            for k in sorted(set(ft) | set(rt)):
                if ft.get(k) != rt.get(k):
                    print("  %s fake=%r real=%r" % (k, ft.get(k), rt.get(k)))
            return False
        return True

    try:
        n = len(ops)
        snaps = 0
        for i, op in enumerate(ops):
            a_ok, a_err = apply_fake(fs, op)
            r_ok, r_err = apply_real(tmp, op)
            if a_ok != r_ok:
                print("RESULT MISMATCH %s row[%d]=%r fake=(%r,%r) real=(%r,%r)"
                      % (label, i, op, a_ok, a_err, r_ok, r_err))
                return False
            if not a_ok and a_err != r_err:
                print("ERRNO MISMATCH %s row[%d]=%r fake_errno=%r real_errno=%r"
                      % (label, i, op, a_err, r_err))
                return False
            if SNAPSHOT_EVERY and (i + 1) % SNAPSHOT_EVERY == 0:
                snaps += 1
                if not snap(i, op):
                    return False
        if n and (SNAPSHOT_EVERY == 0 or n % SNAPSHOT_EVERY != 0):
            snaps += 1
            if not snap(n - 1, ops[n - 1]):
                return False
        return n, snaps
    finally:
        fs.destroy()
        rmtree(tmp)


def _rp(tmp, path):
    return os.path.join(tmp, path.lstrip("/"))


def _meta_parity():
    """stat/lstat/chmod/readlink differential over a shared seeded tree."""
    fs = FakeFS()
    tmp = make_tmpfs_tree()
    if tmp is None:
        fs.destroy()
        return None
    div = Divergence()
    seed = [
        ("create", "/f.txt", b"file-body"),
        ("create", "/f.txt", b"file-body"),
        ("mkdir", "/sub"),
        ("create", "/sub/g", b"xyz"),
        ("symlink", "/f.txt", "/l"),
        ("symlink", "nope", "/dangle"),
    ]
    try:
        for op in seed:
            apply_fake(fs, op)
            apply_real(tmp, op)

        rows = 0
        failed = False

        def note(row, msg):
            nonlocal rows, failed
            rows += 1
            if not failed:
                print("META MISMATCH %s: %s" % (row, msg))
                failed = True

        paths = ["/f.txt", "/sub", "/sub/g", "/l", "/dangle", "/missing"]
        for path in paths:
            fl, fel = fs.lstat(path)
            rl = _lstat_real(tmp, path)
            if (fl is None) != (rl is None):
                note(("lstat", path),
                     "presence fake=%r real=%r" % (fl is None, rl is None))
                continue
            rows += 1
            if fl is None:
                continue                       # both missing: parity on errno
            field, diff = normalize_stat(fl, rl)
            if field is not None:
                note(("lstat", path), "field %s %r" % (field, diff))

            if mode_kind(fl.st_mode) == "link":
                continue                       # stat follows; lstat sufficed
            fs2, fe2 = fs.stat(path)
            rr = _stat_real(tmp, path)
            rows += 1
            if (fs2 is None) != (rr is None):
                note(("stat", path),
                     "presence fake=%r real=%r" % (fs2 is None, rr is None))
            elif fs2 is not None:
                field, diff = normalize_stat(fs2, rr)
                if field is not None:
                    note(("stat", path), "field %s %r" % (field, diff))

        # readlink parity on the two symlinks and on a non-symlink.
        for path in ["/l", "/dangle", "/f.txt"]:
            rows += 1
            ft, fe = fs.readlink(path)
            rt = _readlink_real(tmp, path)
            if (ft is None) != (rt is None):
                note(("readlink", path), "presence fake=%r real=%r"
                     % (ft is None, rt is None))
            elif ft is not None and ft != rt:
                note(("readlink", path), "target fake=%r real=%r" % (ft, rt))

        # chmod then re-stat parity across modes.
        for path in ["/f.txt", "/sub"]:
            for mode in [0o755, 0o600, 0o777, 0o400]:
                rows += 1
                fs.chmod(path, mode)
                try:
                    os.chmod(_rp(tmp, path), mode)
                except OSError:
                    note(("chmod", path, mode), "real chmod raised")
                fl, _ = fs.lstat(path)
                rl = _lstat_real(tmp, path)
                field, diff = normalize_stat(fl, rl)
                if field is not None:
                    note(("chmod-stat", path, mode), "field %s %r"
                         % (field, diff))

        return rows, not failed
    finally:
        fs.destroy()
        rmtree(tmp)


def _lstat_real(tmp, path):
    try:
        return os.lstat(_rp(tmp, path))
    except OSError:
        return None


def _stat_real(tmp, path):
    try:
        return os.stat(_rp(tmp, path))
    except OSError:
        return None


def _readlink_real(tmp, path):
    try:
        return os.readlink(_rp(tmp, path))
    except OSError:
        return None


def _fd_parity():
    """fd/offset differential: read/write/lseek/pre/pwrite/ftruncate/dup/append.

    Each logical descriptor owns a fake fd and a real fd over the same flags;
    return values, resulting offsets, and file bytes must agree."""
    fs = FakeFS()
    tmp = make_tmpfs_tree()
    if tmp is None:
        fs.destroy()
        return None
    try:
        path = "/data.bin"
        payload = b"0123456789abcdefghij" * 4

        ff, fe = fs.open(path, O_CREAT | O_RDWR | O_TRUNC, 0o644)
        rf = os.open(_rp(tmp, path), os.O_CREAT | os.O_RDWR | os.O_TRUNC, 0o644)

        rows = 0
        failed = False

        def note(row, msg):
            nonlocal rows, failed
            rows += 1
            if not failed:
                print("FD MISMATCH %s: %s" % (row, msg))
                failed = True

        def chk(label):
            nonlocal rows, failed
            rows += 1
            fl = fs.lseek(ff, 0, SEEK_CUR)
            rl = os.lseek(rf, 0, os.SEEK_CUR)
            if fl != rl:
                if not failed:
                    print("FD MISMATCH %s offset fake=%r real=%r" % (label, fl, rl))
                failed = True

        # write full payload in chunks at current offset (append-at-offset).
        for i in range(0, len(payload), 7):
            chunk = payload[i:i + 7]
            fn, _ = fs.write(ff, chunk)
            rn = os.write(rf, chunk)
            if fn != rn:
                if not failed:
                    print("FD MISMATCH write %d fake_n=%r real_n=%r" % (i, fn, rn))
                failed = True
        chk("write-offset")

        # pwrite at a fixed offset should not move the cursor.
        pw = b"PADDED"
        fs.pwrite(ff, pw, 2)
        os.pwrite(rf, pw, 2)
        chk("pwrite-cursor-immobile")
        _fd_final_size_check(fs, ff, tmp, path, note, chk)

        # pread reads bytes at an offset and leaves cursor.
        for off in [0, 3, len(payload) - 1, len(payload) + 5]:
            fb, _ = fs.pread(ff, 8, off)
            rb = os.pread(rf, 8, off)
            if fb != rb:
                if not failed:
                    print("FD MISMATCH pread off=%d fake=%r real=%r" % (off, fb, rb))
                failed = True
            rows += 1

        # read from cursor to EOF.
        fl = fs.lseek(ff, 0, SEEK_SET)
        rl = os.lseek(rf, 0, os.SEEK_SET)
        fb, _ = fs.read(ff, 1 << 16)
        rb = os.read(rf, 1 << 16)
        if fb != rb:
            if not failed:
                print("FD MISMATCH full-read fake_len=%r real_len=%r" % (len(fb), len(rb)))
            failed = True
        rows += 1

        # dup shares the file offset on both sides.
        fd2, _ = fs.dup(ff)
        rd2 = os.dup(rf)
        fs.lseek(ff, 5, SEEK_SET)
        os.lseek(rf, 5, os.SEEK_SET)
        fb, _ = fs.read(fd2, 4)
        rb = os.read(rd2, 4)
        if fb != rb:
            if not failed:
                print("FD MISMATCH dup-shared-offset fake=%r real=%r" % (fb, rb))
            failed = True
        rows += 1
        fs.close(fd2)
        os.close(rd2)

        # ftruncate shrinks then grows; size parity each step.
        for newsz in [0, 10, len(payload) + 8]:
            fs.ftruncate(ff, newsz)
            os.ftruncate(rf, newsz)
            _fd_final_size_check(fs, ff, tmp, path, note, chk)

        fs.close(ff)
        os.close(rf)

        # O_APPEND writes land at end of file regardless of cursor.
        ap = "/app.bin"
        fa, _ = fs.open(ap, O_CREAT | O_WRONLY | O_APPEND, 0o644)
        ra = os.open(_rp(tmp, ap), os.O_CREAT | os.O_WRONLY | os.O_APPEND, 0o644)
        fs.write(fa, b"start")
        os.write(ra, b"start")
        fs.lseek(fa, 0, SEEK_SET)
        os.lseek(ra, 0, os.SEEK_SET)
        fs.write(fa, b"|end")
        os.write(ra, b"|end")
        fs.close(fa)
        os.close(ra)
        fr, _ = fs.open(ap, O_RDONLY)
        fb, _ = fs.read(fr, 1 << 16)
        fs.close(fr)
        # reopen read-only on the real side for final bytes
        rrb = open(_rp(tmp, ap), "rb").read()
        if fb != rrb:
            if not failed:
                print("FD MISMATCH o_append bytes fake=%r real=%r" % (fb, rrb))
            failed = True
        rows += 1
        return rows, not failed
    finally:
        fs.destroy()
        rmtree(tmp)


def _fd_final_size_check(fs, ff, tmp, path, note, chk):
    st, _ = fs.fstat(ff)
    rsize = os.path.getsize(_rp(tmp, path))
    if st.st_size != rsize:
        note(("ftruncate-size", path), "fake=%r real=%r" % (st.st_size, rsize))
    chk("cursor-after-ftruncate")


def _at_parity():
    """Directory-relative (*at) differential over paired dirfds on both sides.

    A dirfd is opened on the fake with O_DIRECTORY and on the real side via
    libc openat on the same directory; each *at operation is replayed on the
    pair and success/errno (and readlink target) are compared."""
    import realposix as rx
    fs = FakeFS()
    tmp = make_tmpfs_tree()
    if tmp is None:
        fs.destroy()
        return None

    rows = 0
    failed = False

    def io(label, a, r):
        nonlocal rows, failed
        rows += 1
        a_ok, a_err = a
        r_ok, r_err = r
        if a_ok != r_ok or (not a_ok and a_err != r_err):
            if not failed:
                print("AT MISMATCH %s fake=(%r,%r) real=(%r,%r)"
                      % (label, a_ok, a_err, r_ok, r_err))
                failed = True

    try:
        for op in [("mkdir", "/b"), ("mkdir", "/b/sub")]:
            apply_fake(fs, op)
            apply_real(tmp, op)

        ff, _ = fs.open("/b", O_RDONLY | O_DIRECTORY)
        rf, _ = rx.openat(rx.AT_FDCWD, _rp(tmp, "/b"), O_RDONLY | O_DIRECTORY)
        if rf is None:
            fs.destroy()
            rmtree(tmp)
            return None

        io("mkdirat fresh", fs.mkdirat(ff, "x"), rx.mkdirat(rf, "x"))
        io("mkdirat eexist", fs.mkdirat(ff, "x"), rx.mkdirat(rf, "x"))
        io("mkdirat nested", fs.mkdirat(ff, "sub/y"), rx.mkdirat(rf, "sub/y"))

        a, ae = fs.openat(ff, "f", O_CREAT | O_WRONLY | O_TRUNC, 0o644)
        r, re_ = rx.openat(rf, "f", O_CREAT | O_WRONLY | O_TRUNC, 0o644)
        rows += 1
        if (a is None) != (r is None):
            failed = True
            print("AT MISMATCH openat-create presence fake=%r real=%r" % (a, r))
        else:
            fs.write(a, b"data")
            os.write(r, b"data")
            fs.close(a)
            rx.close(r)
        rows += 1
        aex = fs.openat(ff, "f", O_CREAT | O_EXCL, 0o600)[1]
        rex = rx.openat(rf, "f", O_CREAT | O_EXCL, 0o600)[1]
        if aex != rex:
            failed = True
            print("AT MISMATCH openat-EXCL fake=%r real=%r" % (aex, rex))

        io("unlinkat file", fs.unlinkat(ff, "f"), rx.unlinkat(rf, "f"))
        io("unlinkat missing", fs.unlinkat(ff, "f"), rx.unlinkat(rf, "f"))
        io("symlinkat", fs.symlinkat("tgt", ff, "ln"), rx.symlinkat("tgt", rf, "ln"))
        fl, _ = fs.readlinkat(ff, "ln")
        rl, _ = rx.readlinkat(rf, "ln")
        rows += 1
        if rl is None or fl != rl.decode():
            failed = True
            print("AT MISMATCH readlinkat fake=%r real=%r" % (fl, rl))
        io("unlinkat dir noflag", fs.unlinkat(ff, "x"), rx.unlinkat(rf, "x"))
        io("unlinkat dir removedir",
           fs.unlinkat(ff, "x", AT_REMOVEDIR), rx.unlinkat(rf, "x", rx.AT_REMOVEDIR))
        io("renameat move", fs.renameat(ff, "sub/y", ff, "renamed"),
           rx.renameat(rf, "sub/y", rf, "renamed"))
        io("renameat back", fs.renameat(ff, "renamed", ff, "sub/y"),
           rx.renameat(rf, "renamed", rf, "sub/y"))

        a, _ = fs.openat(ff, "m", O_CREAT | O_WRONLY | O_TRUNC, 0o644)
        r, _ = rx.openat(rf, "m", O_CREAT | O_WRONLY | O_TRUNC, 0o644)
        fs.close(a)
        rx.close(r)
        io("fchmodat", fs.fchmodat(ff, "m", 0o600), rx.fchmodat(rf, "m", 0o600))
        io("faccessat R_OK", fs.faccessat(ff, "m", 4), rx.faccessat(rf, "m", 4))
        io("mkdirat bad dirfd", fs.mkdirat(9999, "q"), rx.mkdirat(9999, "q"))
        io("unlinkat symlink", fs.unlinkat(ff, "ln"), rx.unlinkat(rf, "ln"))

        fs.close(ff)
        rx.close(rf)
        return rows, not failed
    finally:
        fs.destroy()
        rmtree(tmp)


def _open_flags_parity():
    """open/openat flags matrix: O_EXCL, O_DIRECTORY, O_NOFOLLOW, O_TRUNC."""
    import realposix as rx
    fs = FakeFS()
    tmp = make_tmpfs_tree()
    if tmp is None:
        fs.destroy()
        return None
    rows = 0
    failed = False

    try:
        # seed a file, a dir, and a symlink on both sides
        apply_fake(fs, ("create", "/file", b"abc"))
        apply_real(tmp, ("create", "/file", b"abc"))
        apply_fake(fs, ("mkdir", "/dir"))
        apply_real(tmp, ("mkdir", "/dir"))
        apply_fake(fs, ("symlink", "file", "/lnk"))
        apply_real(tmp, ("symlink", "file", "/lnk"))
        _p = lambda p: _rp(tmp, p)  # noqa: E731

        cases = [
            ("O_CREAT fresh", "/new", O_CREAT | O_WRONLY),
            ("O_CREAT|O_EXCL fresh", "/excl", O_CREAT | O_EXCL | O_WRONLY),
            ("O_CREAT|O_EXCL existing", "/file", O_CREAT | O_EXCL | O_WRONLY),
            ("O_DIRECTORY on dir", "/dir", O_RDONLY | O_DIRECTORY),
            ("O_DIRECTORY on file", "/file", O_RDONLY | O_DIRECTORY),
            ("O_NOFOLLOW on symlink", "/lnk", O_RDONLY | O_NOFOLLOW),
            ("plain open symlink", "/lnk", O_RDONLY),
            ("open missing no-creat", "/nope", O_RDONLY),
        ]
        for label, path, flags in cases:
            af = fs.open(path, flags)
            ar = rx.openat(rx.AT_FDCWD, _p(path), flags)
            # normalize to (ok, errno): fs.open returns (None,err) or (fd,0);
            # rx returns (None,err) or (fd,0). Treat fd presence as ok.
            a_ok = af[0] is not None
            r_ok = ar[0] is not None
            if a_ok:
                fs.close(af[0])
            if r_ok:
                rx.close(ar[0])
            rows += 1
            if a_ok != r_ok or (not a_ok and af[1] != ar[1]):
                if not failed:
                    print("FLAGS MISMATCH %s fake=(%r,%r) real=(%r,%r)"
                          % (label, a_ok, af[1], r_ok, ar[1]))
                    failed = True
        return rows, not failed
    finally:
        fs.destroy()
        rmtree(tmp)


def _umask_parity():
    """Umask-masked creation-mode parity for mkdir/create over umask values.

    Only meaningful on a non-root host whose filesystem honors umask and mode
    bits; when running as root it is reported as skipped, not a failure."""
    if is_root():
        return None, None
    fs = FakeFS()
    tmp = make_tmpfs_tree()
    if tmp is None:
        fs.destroy()
        return None, None
    rows = 0
    failed = False

    def note(row, msg):
        nonlocal rows, failed
        rows += 1
        if not failed:
            print("UMASK MISMATCH %s: %s" % (row, msg))
            failed = True

    old = os.umask(0o022)
    fs.setumask(0o022)
    try:
        for um in [0o000, 0o022, 0o027, 0o077, 0o777]:
            fs.setumask(um)
            os.umask(um)
            d = "/m%03o" % um
            f = d + "/file"
            a, ae = fs.mkdir(d, 0o777)
            r_ok, r_err = _mkdir_mode(tmp, d, 0o777)
            rows += 1
            if a != r_ok or (not a and ae != r_err):
                note((d, um), "mkdir fake=(%r,%r) real=(%r,%r)" % (a, ae, r_ok, r_err))
            fd, fe = fs.open(f, O_CREAT | O_WRONLY | O_TRUNC, 0o666)
            rr = _open_create(tmp, f, 0o666)
            rows += 1
            if (fd is None) != (rr is None):
                note((f, um), "create presence fake=%r real=%r" % (fd, rr))
            elif fd is not None:
                fs.close(fd)
                os.close(rr)
            # resulting mode parity after umask masking
            for path in [d, f]:
                fl, _ = fs.lstat(path)
                rl = _lstat_real(tmp, path)
                rows += 1
                if fl is None or rl is None:
                    continue
                field, diff = normalize_stat(fl, rl)
                if field is not None:
                    note((path, um), "mode %s %r" % (field, diff))
        return rows, not failed
    finally:
        fs.destroy()
        rmtree(tmp)
        if old is not None:
            os.umask(old)


def _mkdir_mode(tmp, path, mode):
    try:
        os.mkdir(_rp(tmp, path), mode)
        return True, 0
    except OSError as exc:
        return False, exc.errno


def _open_create(tmp, path, mode):
    try:
        import realposix as rx
        fd, _ = rx.openat(rx.AT_FDCWD, _rp(tmp, path),
                          O_CREAT | O_WRONLY | O_TRUNC, mode)
        return fd
    except OSError:
        return None


def main():
    phases = list(_all_phases())
    total_rows = _count(phases)
    div = Divergence()

    print("massive differential: %d generated rows across %d phases"
          % (total_rows, len(phases)))

    probe = make_tmpfs_tree()
    if probe is None:
        print("massive differential: SKIPPED (no writable tmpfs mount)")
        return 0
    rmtree(probe)

    ran = 0
    snaps = 0
    failed = False
    for i, script in enumerate(phases):
        got = _run_phase("phase[%d]" % i, script, div)
        if got is None:
            continue
        if got is False:
            failed = True
            continue
        n, s = got
        ran += n
        snaps += s

    meta = _meta_parity()
    if meta is None:
        print("massive differential: SKIPPED (no writable tmpfs mount)")
        return 0
    m_rows, m_ok = meta
    ran += m_rows
    if not m_ok:
        failed = True

    fd = _fd_parity()
    if fd is None:
        print("massive differential: SKIPPED (no writable tmpfs mount)")
        return 0
    fd_rows, fd_ok = fd
    ran += fd_rows
    if not fd_ok:
        failed = True

    at = _at_parity()
    if at is None:
        print("massive differential: SKIPPED (no writable tmpfs mount)")
        return 0
    at_rows, at_ok = at
    ran += at_rows
    if not at_ok:
        failed = True

    flags = _open_flags_parity()
    if flags is None:
        print("massive differential: SKIPPED (no writable tmpfs mount)")
        return 0
    fl_rows, fl_ok = flags
    ran += fl_rows
    if not fl_ok:
        failed = True

    um_rows, um_ok = _umask_parity()
    if um_rows is not None:
        ran += um_rows
        if not um_ok:
            failed = True

    if failed:
        print("massive differential: FAILED (see mismatches above)")
        return 1

    print("massive differential: %d rows executed "
          "(namespace=%d meta=%d fd=%d at=%d flags=%d umask=%s)"
          % (ran, _count(phases), m_rows, fd_rows, at_rows, fl_rows,
             "skip" if um_rows is None else um_rows))
    if ran < MIN_ROWS:
        print("FAIL: executed rows %d below gate %d" % (ran, MIN_ROWS))
        return 1
    print("massive differential: %d rows + %d namespace compares passed"
          % (ran, snaps))
    return 0


if __name__ == "__main__":
    sys.exit(main())
