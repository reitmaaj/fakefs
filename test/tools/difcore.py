"""Shared engine for the massive tmpfs-vs-:memory: differential suite.

Each scenario runs an operation against both a real tmpfs tree (reference)
and a :memory: libfakefs volume (subject), asserting parity of
success/errno (and return value where meaningful), the normalized namespace
after every mutation, and (for fd operations) file offset and open-flag
state.

Environment-specific and known-deviation cases are never silently ignored:
they are routed through a Divergence registry that records a *registered
skip* with a reason. The engine counts executed rows and registered skips so
a row-count gate can forbid silent shrinkage."""

import os
import stat as pstat
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ffsffi import (  # noqa: E402
    S_IFMT, S_IFDIR, S_IFREG, S_IFLNK,
    O_CREAT, O_EXCL, O_TRUNC, O_APPEND, O_WRONLY, O_RDWR, O_RDONLY,
    O_DIRECTORY, O_NOFOLLOW,
)
from difsupport import tree, fake_tree, make_tmpfs_tree, rmtree  # noqa: E402

# Normalized namespace key comparison is provided by difsupport.tree /
# difsupport.fake_tree (path -> (type, target|bytes)).  The registry and
# parity engine below add skip accounting and fd/offset semantics.

# Portable open flags that may legally differ in how a host reports them back
# via F_GETFL; everything else must match exactly.
_PORTABLE_FD_FLAGS = O_APPEND | O_DIRECTORY | O_NOFOLLOW | O_TRUNC | O_CREAT | O_EXCL


def errno_name(e):
    """Return a readable name for an errno int (for diagnostics only)."""
    import errno as _e
    for name in dir(_e):
        if getattr(_e, name) == e and name.isupper():
            return name
    return str(e)


class Outcome:
    """A per-operation result on one side: ok + errno (int)."""

    __slots__ = ("ok", "err")

    def __init__(self, ok, err=0):
        self.ok = ok
        self.err = err

    def __repr__(self):
        if self.ok:
            return "ok"
        return "fail(%s)" % errno_name(self.err)


class Fd:
    """A logical descriptor pairing one fakefs fd with one real fd that share
    the same open flags and refer to the same open file description."""

    __slots__ = ("fake", "real")

    def __init__(self, fake, real):
        self.fake = fake
        self.real = real


class Divergence:
    """Registered reasons for environment-specific or known-deviation skips.

    Every skip must be registered up front under a (family, key) pair; the
    engine fails if it would skip a row that has no registered reason."""

    def __init__(self):
        self._reasons = {}
        self.skipped = []

    def register(self, family, key, reason):
        self._reasons[(family, key)] = reason

    def lookup(self, family, key):
        return self._reasons.get((family, key))

    def note_skip(self, family, key, row):
        self.skipped.append((family, key, row))

    def unregistered(self):
        return set(self.skipped) - set(
            (family, key) for (family, key) in self._reasons
        )


def is_root():
    return hasattr(os, "geteuid") and os.geteuid() == 0


def align_credentials(fs, umask):
    """Align reference (host umask) and subject (ffs umask/ids).

    Running permission assertions strictly requires a non-root host with a
    real filesystem that honors mode bits; callers guard with is_root()."""
    if is_root():
        return
    old = os.umask(umask & 0o777)
    fs.setumask(umask & 0o777)
    euid, egid = os.geteuid(), os.getegid()
    fs.setids(euid, egid)
    return old


def mode_kind(mode):
    t = mode & S_IFMT
    if t == S_IFDIR:
        return "dir"
    if t == S_IFLNK:
        return "link"
    if t == S_IFREG:
        return "file"
    return "other"


def normalize_stat(fake_st, real_st):
    """Return (None,None) on agreement of contract fields, else a description.

    Contract fields compared: object type and permission bits, and size for
    regular files only.  Inode, timestamps, and link counts are host-specific
    and excluded; directory and symlink sizes are also host-specific."""
    if mode_kind(fake_st.st_mode) != mode_kind(real_st.st_mode):
        return "type", (mode_kind(fake_st.st_mode), mode_kind(real_st.st_mode))
    fperm = fake_st.st_mode & 0o7777
    rperm = real_st.st_mode & 0o7777
    if fperm != rperm:
        return "mode", (fperm, rperm)
    if mode_kind(fake_st.st_mode) == "file":
        if fake_st.st_size != real_st.st_size:
            return "size", (fake_st.st_size, real_st.st_size)
    return None, None


def unlink_kind(path):
    """Return the normalized entry kind at path on the real side, or None."""
    try:
        return mode_kind(os.lstat(path).st_mode)
    except OSError:
        return None
