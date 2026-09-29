#!/bin/sh -eu

set -eu

cd "$(dirname "$0")/.."

archive="build/libfakefs.a"

for t in test/unit/test_*.c; do
    name="$(basename "$t" .c)"
    cc -std=c89 -pedantic -Wall -Wextra -Werror -Wno-long-long \
        -Iinclude -Isrc -Itest -Itest/unit \
        "$t" "$archive" -lsqlite3 -o "build/unit_$name"
    printf '== %s ==\n' "$name"
    "build/unit_$name"
done
