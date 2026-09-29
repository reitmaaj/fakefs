#!/usr/bin/env python3
"""Differential test: run an identical operation script against the real
host filesystem (a tmpdir) and libfakefs, then compare success/failure
(errno) for every operation and the resulting namespace byte-for-byte."""

import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from difsupport import apply_fake, apply_real, fake_tree, tree  # noqa: E402
from ffsffi import FakeFS  # noqa: E402

SCRIPT = [
    ("mkdir", "/d"),
    ("mkdir", "/d/sub"),
    ("create", "/d/sub/b.txt", b"hello world"),
    ("create", "/top.txt", b"top content"),
    ("link", "/top.txt", "/d/toplink"),
    ("symlink", "sub/b.txt", "/d/link"),
    ("truncate", "/d/toplink", 3),
    ("append", "/d/sub/b.txt", b" MORE"),
    ("create", "/d/sub/c.txt", b"zzz"),
    ("rename", "/d/sub/c.txt", "/d/sub/renamed.txt"),
    ("unlink", "/d/sub/renamed.txt"),
    ("mkdir", "/d/sub/deep"),
    ("rmdir", "/d/sub"),           # ENOTEMPTY
    ("chmod", "/top.txt", 0o600),
    ("read", "/d/sub/b.txt", b"hello world MORE"),
    ("read", "/d/link", b"hello world MORE"),
    ("read", "/d/toplink", b"top"),
]


def main():
    fs = FakeFS()
    tmp = tempfile.mkdtemp(prefix="fakefs_diff_")

    try:
        for i, op in enumerate(SCRIPT):
            a_ok, a_err = apply_fake(fs, op)
            r_ok, r_err = apply_real(tmp, op)
            if a_ok != r_ok:
                print("RESULT MISMATCH op[%d]=%r fake=(%r,%r) real=(%r,%r)"
                      % (i, op, a_ok, a_err, r_ok, r_err))
                sys.exit(1)
            if not a_ok and a_err != r_err:
                print("ERRNO MISMATCH op[%d]=%r fake_errno=%r real_errno=%r"
                      % (i, op, a_err, r_err))
                sys.exit(1)

        ft = fake_tree(fs)
        rt = tree(tmp)
        if ft != rt:
            print("TREE MISMATCH")
            for k in sorted(set(ft) | set(rt)):
                if ft.get(k) != rt.get(k):
                    print("  %s fake=%r real=%r" % (k, ft.get(k), rt.get(k)))
            sys.exit(1)

        print("differential: %d ops + full-tree compare passed" % len(SCRIPT))
    finally:
        fs.destroy()
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
