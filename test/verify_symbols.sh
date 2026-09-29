#!/bin/sh -eu

cd "$(dirname "$0")/.."

so="build/libfakefs.so"

if [ ! -f "$so" ]; then
    printf 'missing %s; run make first\n' "$so" >&2
    exit 2
fi

ext_symbols='ffs_txn_begin|ffs_txn_commit|ffs_txn_rollback|ffs_vcs_checkin|ffs_vcs_checkout|ffs_vcs_list|ffs_vcs_version|ffs_util_dump'

assert_absent()
{
    if nm -D "$so" 2>/dev/null | grep -E "$1" >/dev/null; then
        printf 'unexpected symbol(s) matching "%s" in %s\n' "$1" "$so" >&2
        exit 1
    fi
}

assert_only_ffs()
{
    if nm -D --defined-only "$so" 2>/dev/null |
        awk 'NF >= 3 && $2 ~ /^[TtDdBbRrWwVv]$/ {print $NF}' |
        grep -v '^ffs_' >/dev/null; then
        printf 'internal symbol(s) leaked into %s\n' "$so" >&2
        exit 1
    fi
}

assert_absent "$ext_symbols"
assert_absent 'sqlite3session_'
assert_only_ffs

printf 'verify_symbols: core-only build OK\n'
