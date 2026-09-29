#!/bin/sh -eu

set -eu

cd "$(dirname "$0")/.."

archive="build/libfakefs.a"

cc -std=c89 -pedantic -Wall -Wextra -Werror -Wno-long-long -Iinclude \
    test/e2e/persist_helper.c "$archive" -lsqlite3 \
    -o build/persist_helper

cc -std=c89 -pedantic -Wall -Wextra -Werror -Wno-long-long -Iinclude \
    test/tools/fuzz.c "$archive" -lsqlite3 \
    -o build/fuzz

db="build/e2e_persist.db"
rm -f "$db"

printf '== test_persistence_process ==\n'
./build/persist_helper "$db" write
./build/persist_helper "$db" verify

printf '== test_fuzz ==\n'
./build/fuzz "${FAKEFS_FUZZ_OPS:-100000}"

rm -f "$db"
