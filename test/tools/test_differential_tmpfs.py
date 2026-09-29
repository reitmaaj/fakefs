#!/usr/bin/env python3
"""Differential test suite: a :memory: libfakefs volume vs a real kernel
filesystem rooted on a tmpfs mount.

Two operation streams are replayed against both sides:
  1. a curated deterministic smoke script; and
  2. a seeded random walk (reusing the property generator/model).

After every step the per-operation success/errno and the normalized
namespace must agree. The suite skips (exit 0) when no writable tmpfs mount
is available, so it never breaks tmpfs-less build hosts."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from difsupport import (  # noqa: E402
    apply_fake,
    apply_real,
    fake_tree,
    tree,
    make_tmpfs_tree,
    rmtree,
)
from ffsffi import FakeFS  # noqa: E402
from gen import Generator  # noqa: E402
from model import Model  # noqa: E402

SMOKE = [
    ("mkdir", "/d"),
    ("mkdir", "/d/sub"),
    ("mkdir", "/d/sub/deep"),
    ("rmdir", "/d/sub"),                # ENOTEMPTY
    ("create", "/d/sub/a.txt", b"alpha"),
    ("create", "/d/sub/a.txt", b"overwritten"),   # recreate same name
    ("create", "/top.bin", b"\x00\x01\x02"),
    ("truncate", "/top.bin", 0),
    ("append", "/d/sub/a.txt", b" beta"),
    ("link", "/d/sub/a.txt", "/d/hard"),
    ("symlink", "sub/a.txt", "/d/rel"),
    ("symlink", "/d/missing", "/d/dangle"),
    ("read", "/d/rel", b"overwritten beta"),
    ("read", "/d/hard", b"overwritten beta"),
    ("readlink_check", "/d/rel", "sub/a.txt"),
    ("rename", "/d/sub/deep", "/d/sub/deep2"),   # empty dir rename
    ("rename", "/d/sub/a.txt", "/d/sub/renamed"),
    ("unlink", "/d/sub/renamed"),
    ("unlink", "/d/rel"),               # unlink removes the link
    ("mkdir", "/d/sub/deep2/inner"),
    ("rmdir", "/d/sub"),                # still ENOTEMPTY
    ("create", "/d/only", b"x"),
    ("chmod", "/d/only", 0o600),
    ("unlink", "/d/only"),
    ("create", "/big", b"z" * 50000),
    ("truncate", "/big", 100000),       # sparse extension
    ("read", "/big", b"z" * 50000 + b"\0" * 50000),
]


def run_stream(label, ops):
    """Replay ops on both sides, asserting per-op and per-step parity."""
    fs = FakeFS()
    tmp = make_tmpfs_tree()
    if tmp is None:
        fs.destroy()
        return "skip"

    try:
        for i, op in enumerate(ops):
            if op[0] == "readlink_check":
                rl, re_ = fs.readlink(op[1])
                if rl != op[2] or re_ != 0:
                    print("READLINK MISMATCH op[%d]=%r fake=(%r,%r)"
                          % (i, op, rl, re_))
                    return "fail"
                continue
            a_ok, a_err = apply_fake(fs, op)
            r_ok, r_err = apply_real(tmp, op)
            if a_ok != r_ok:
                print("RESULT MISMATCH op[%d]=%r fake=(%r,%r) real=(%r,%r)"
                      % (i, op, a_ok, a_err, r_ok, r_err))
                return "fail"
            if not a_ok and a_err != r_err:
                print("ERRNO MISMATCH op[%d]=%r fake_errno=%r real_errno=%r"
                      % (i, op, a_err, r_err))
                return "fail"
            if (op[0] == "read") and not a_ok:
                continue
            ft = fake_tree(fs)
            rt = tree(tmp)
            if ft != rt:
                print("SNAPSHOT MISMATCH at op[%d]=%r" % (i, op))
                for k in sorted(set(ft) | set(rt)):
                    if ft.get(k) != rt.get(k):
                        print("  %s fake=%r real=%r" % (k, ft.get(k), rt.get(k)))
                return "fail"
        print("%s: %d ops + full-tree compare passed" % (label, len(ops)))
        return "pass"
    finally:
        fs.destroy()
        rmtree(tmp)


def run_random(seed, nops):
    fs = FakeFS()
    tmp = make_tmpfs_tree()
    if tmp is None:
        fs.destroy()
        return "skip"
    model = Model()
    gen = Generator(seed)

    try:
        for i in range(nops):
            op = gen.next_op(model)
            a_ok, a_err = apply_fake(fs, op)
            r_ok, r_err = apply_real(tmp, op)
            if a_ok != r_ok:
                print("RESULT MISMATCH seed=%d step=%d op=%r fake=(%r,%r) "
                      "real=(%r,%r)" % (seed, i, op, a_ok, a_err, r_ok, r_err))
                return "fail"
            if not a_ok and a_err != r_err:
                print("ERRNO MISMATCH seed=%d step=%d op=%r fake_errno=%r "
                      "real_errno=%r" % (seed, i, op, a_err, r_err))
                return "fail"
            apply_model(model, op)
            ft = fake_tree(fs)
            rt = tree(tmp)
            if ft != rt:
                print("SNAPSHOT MISMATCH seed=%d step=%d op=%r" % (seed, i, op))
                for k in sorted(set(ft) | set(rt)):
                    if ft.get(k) != rt.get(k):
                        print("  %s fake=%r real=%r" % (k, ft.get(k), rt.get(k)))
                return "fail"
        print("differential tmpfs: %d ops passed (seed=%d)" % (nops, seed))
        return "pass"
    finally:
        fs.destroy()
        rmtree(tmp)


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
    if kind == "rename":
        return model.rename(op[1], op[2])
    if kind == "create":
        return model.create_file(op[1], op[2])
    if kind == "overwrite":
        return model.overwrite_file(op[1], op[2])
    if kind == "append":
        return model.append_file(op[1], op[2])
    if kind == "truncate":
        return model.truncate_file(op[1], op[2])
    raise ValueError("unknown op %r" % (kind,))


def main():
    nops = int(os.environ.get("NOPS", "400"))
    seeds = [int(s) for s in os.environ.get("SEEDS", "1 2 3").split()]

    outcomes = []
    outcomes.append(run_stream("smoke script", SMOKE))
    for seed in seeds:
        outcomes.append(run_random(seed, nops))

    if all(o == "skip" for o in outcomes):
        print("differential tmpfs: SKIPPED (no writable tmpfs mount)")
        return 0
    if any(o == "fail" for o in outcomes):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
