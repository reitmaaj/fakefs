# fakefs

A small, embeddable, transactional fake filesystem implemented as a C89
library (`libfakefs`) backed by a single SQLite database.

## Intent

`fakefs` presents a POSIX-like filesystem API (`ffs_*` functions) over a
single SQLite database, so application and test code can exercise filesystem
behavior against an in-process, transactional store instead of the real host
filesystem. SQLite provides atomicity and transactional persistence with no
dependencies beyond `libsqlite3` (durability applies only to disk-backed
databases, not `ffs_create_memory`). It is filesystem-only: there are no
extensions such as version control, explicit transactions, or volume export.

### Goals

- Cover the most important filesystem operations and metadata (see Features).
- Offer an in-memory mode (`ffs_create_memory`) for fast, isolated, deterministic tests.
- Provide POSIX-like semantics for hard links, symlinks, and permission checks.
- Make every operation atomic (transaction-wrapped) and map failures to `errno`.

### Non-goals

- No mountable (FUSE) filesystem.
- No extension API: no version control, no explicit user transactions, no
  volume-dump utility. Per-operation atomicity is an internal detail.
- No efficient storage of large or sparse files beyond sparse byte extents
  (see below): file content is stored as non-overlapping extents and is not
  further compressed or content-addressed.
- No extended attributes, ACLs, or file locking.
- No concurrency guarantees beyond what a single SQLite connection provides.

## Features

- Directories: `mkdir`, `rmdir`, `opendir`/`readdir`/`closedir`, `chdir`, `getcwd`
- Regular files: `open`, `read`, `write`, `lseek`, `ftruncate`, `truncate`,
  `pread`, `pwrite`
- Rename: `rename` (file-over-file, dir-over-empty-dir, cross-directory move)
- Hard links: `link`, `unlink` (with `nlink` tracking and open-after-unlink)
- Symbolic links: `symlink`, `readlink`, `realpath` (with loop detection)
- Metadata: `stat`/`lstat`/`fstat`, `chmod`, `chown`, `lchown`, `fchmod`,
  `fchown`, `utimes` (`mode`, `uid`, `gid`, `nlink`, `size`, `ino`, times)
- Permissions: POSIX bit enforcement with a configurable acting identity
  (`ffs_setids`) and `umask` (`ffs_setumask`); opt-in `atime` (`ffs_setatime`)
- Descriptors: `dup`, `dup2`, `fcntl` (`F_GETFL`/`F_SETFL`/`F_DUPFD`),
  `fchdir`, `O_DIRECTORY`, `O_NOFOLLOW`
- Directory-relative operations: `openat`, `mkdirat`, `unlinkat` (with
  `AT_REMOVEDIR`), `linkat` (`AT_SYMLINK_FOLLOW`), `symlinkat`, `readlinkat`,
  `renameat`, `fstatat` (`AT_SYMLINK_NOFOLLOW`), `fchmodat`, `fchownat`,
  `faccessat`, plus `access`
- Statistics: `ffs_statfs`

File content is stored as sparse, non-overlapping byte extents with 64-bit
sizes/offsets and a fixed 64 KiB extent cap. `include/fakefs.h` declares the
complete public API. All functions follow POSIX conventions (`0` on success,
`-1` + `errno` on error), referenced through an opaque `ffs *` handle so
multiple independent filesystems can coexist in one process.

## Build & test

    make              # builds build/libfakefs.a and build/libfakefs.so
    make test         # functional suites: unit, component, integration, e2e, property
    make check        # static checks: shellcheck, symbol gate, complexity gates
    make check-leak   # valgrind memcheck over the fuzz driver and unit tests
    make clean        # removes build artifacts

The individual runner scripts under `test/` remain available directly
(each expects a completed `make`):

    sh test/run_unit.sh            # L1: internal helper unit tests
    sh test/run_lib.sh component   # L2: public API, one file per feature
    sh test/run_lib.sh integration # L3: cross-feature invariants
    sh test/run_e2e.sh             # L4: cross-process persistence + C fuzz
    sh test/run_property.sh        # L4: model-based + POSIX differential (Python)
    sh test/run_leak.sh            # valgrind memcheck (fuzz + unit/component)
    sh test/verify_symbols.sh      # exported-symbol gate on the .so
    sh test/check_complexity.sh    # lizard: >=90% of src/ functions below 10 NLOC

The library compiles under `-std=c89 -pedantic -Wall -Wextra -Werror -Wno-long-long`
(`-D_POSIX_C_SOURCE=200112L` is a base flag) and links against a plain
`libsqlite3` — no session extension, no feature macros, no `src/ext/`.

### Dependencies

- Build: POSIX `make`, a C89-capable C compiler (gcc or clang), `ar`
  (binutils), and `libsqlite3` with headers.
- Test: POSIX `sh` and coreutils (`awk`, `tr`, `grep`); `python3` (stdlib
  only — `ctypes`, no pip packages) for the property and differential
  suites, which compare against the real host filesystem. Linux is
  recommended: the ctypes bindings use Linux constants, and the tmpfs
  differential prefers a writable tmpfs at `/dev/shm`, `/tmp`, or `/run`
  (it falls back gracefully when none is found). `make check-leak` also
  needs `valgrind`.
- Check: `shellcheck` (sh and bash dialects), `lizard` (function-size
  gate), and `nm` (binutils, exported-symbol gate).

## Status

The core filesystem, hard/symlink handling, metadata, permissions, file
descriptor operations, fidelity fixes, and sparse extent content are
implemented and passing.

- Source: `src/` core modules (`db`, `path`, `inode`, `dirent`, `resolve`,
  `perm`, `fdtable`, `ops`, `extent`, `data`) sharing a private
  `src/fakefs_internal.h`; public API in `include/fakefs.h`.
- Tests: a five-level pyramid — 9 unit + 17 component + 7 integration
  binaries plus cross-process persistence, a 100k-op C fuzz driver, a seeded
  model-based property test, and a POSIX differential test — all passing.
- Coverage: ~99% lines / ~100% functions over `src/` (gcov/lcov).
- Lint: shellcheck (sh + bash) on all test runner scripts; C89 conformance
  via `-std=c89 -pedantic -Wall -Wextra -Werror`.

### Known deviations from POSIX

- `st_atime` is not updated on read unless enabled with `ffs_setatime`.
- `ffs_statfs` reports inode count (`f_files`) and total content bytes
  (`f_blocks`); free-space fields are `0` because SQLite grows on demand.

## License

See `LICENSE`.
