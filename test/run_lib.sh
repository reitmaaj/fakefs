#!/bin/sh -eu

set -eu

cd "$(dirname "$0")/.."

level="${1:?usage: run_lib.sh <level>}"
archive="build/libfakefs.a"

for t in "test/$level"/test_*.c; do
    name="$(basename "$t" .c)"
    cc -std=c89 -pedantic -Wall -Wextra -Werror -Wno-long-long -D_POSIX_C_SOURCE=200112L \
        -Iinclude -Itest \
        "$t" "$archive" -lsqlite3 -o "build/${level}_$name"
    printf '== %s ==\n' "$name"
    "build/${level}_$name"
done
