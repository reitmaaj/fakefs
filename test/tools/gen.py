"""Seeded random operation generator for property-based testing."""

import random

NAMES = ["a", "b", "c", "d", "e", "f"]


def rand_data(rng):
    n = rng.randint(0, 32)
    return bytes(rng.choice(b"0123456789abcdefghijklmnopqrstuvwxyz") for _ in range(n))


class Generator:
    def __init__(self, seed):
        self.rng = random.Random(seed)

    def _paths(self, model):
        snap = model.snapshot()
        dirs = [p for p, v in snap.items() if v[0] == "dir"]
        files = [p for p, v in snap.items() if v[0] == "file"]
        links = [p for p, v in snap.items() if v[0] == "link"]
        return snap, dirs, files, links

    def _fresh(self, model, parent):
        """A path under parent whose name does not exist as any entry. A name
        is taken when either the file/link key or the directory key (which
        carries a trailing slash in the snapshot) is present."""
        rng = self.rng
        for _ in range(40):
            name = rng.choice(NAMES)
            path = parent + name
            snap, _, _, _ = self._paths(model)
            if path not in snap and (path + "/") not in snap:
                return path
        i = 0
        while True:
            path = parent + ("n%d" % i)
            snap, _, _, _ = self._paths(model)
            if path not in snap and (path + "/") not in snap:
                return path
            i += 1

    def _nonroot_dirs(self, dirs):
        return [d for d in dirs if d != "/"]

    def _dir_parent(self, dirs):
        c = self._nonroot_dirs(dirs)
        return self.rng.choice(c) if c else "/"

    def _grow(self, model):
        """Empty/subject-less state: emit a benign creation instead of a
        degenerate destructive op whose subject would be the root inode."""
        return ("create", self._fresh(model, "/"), rand_data(self.rng))

    def next_op(self, model):
        rng = self.rng
        snap, dirs, files, links = self._paths(model)
        existing = list(snap.keys())
        kind = rng.choice(
            ["mkdir", "rmdir", "unlink", "link", "symlink",
             "create", "overwrite", "append", "truncate", "rename"]
        )

        # The root inode "/" is never used as a destructive or linking
        # subject: a real filesystem cannot remove/link the directory it is
        # mounted in, and libfakefs refuses to destroy its virtual root, so
        # those states are not comparable between the two implementations.

        if kind == "mkdir":
            return ("mkdir", self._fresh(model, self._dir_parent(dirs)))

        if kind == "rmdir":
            c = self._nonroot_dirs(dirs)
            if not c:
                return self._grow(model)
            return ("rmdir", rng.choice(c))

        if kind == "unlink":
            c = files + links
            if not c:
                return self._grow(model)
            return ("unlink", rng.choice(c))

        if kind == "link":
            if not files:
                return self._grow(model)
            src = rng.choice(files)
            dst = self._fresh(model, self._dir_parent(dirs))
            return ("link", src, dst)

        if kind == "symlink":
            tgt = rng.choice(existing + ["nonexistent-target"])
            return ("symlink", tgt, self._fresh(model, self._dir_parent(dirs)))

        if kind == "create":
            return ("create", self._fresh(model, self._dir_parent(dirs)),
                    rand_data(rng))

        if kind == "overwrite":
            if not files:
                return self._grow(model)
            return ("overwrite", rng.choice(files), rand_data(rng))

        if kind == "append":
            if not files:
                return self._grow(model)
            return ("append", rng.choice(files), rand_data(rng))

        if kind == "truncate":
            if not files:
                return self._grow(model)
            return ("truncate", rng.choice(files), rng.randint(0, 64))

        # rename regular files/links to a fresh name (avoids the overwrite,
        # dir-into-subtree, and root conflict cases whose errno differs
        # between a real mount root and the virtual root).
        c = files + links
        if not c:
            return self._grow(model)
        src = rng.choice(c)
        dst = self._fresh(model, self._dir_parent(dirs))
        return ("rename", src, dst)
