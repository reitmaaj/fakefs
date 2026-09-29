#!/bin/sh -eu

set -eu

cd "$(dirname "$0")/.."

cc -std=c89 -pedantic -Wall -Wextra -Werror -Wno-long-long -Iinclude \
    test/tools/fuzz.c build/libfakefs.a -lsqlite3 -o build/fuzz_leak

valgrind --leak-check=full --errors-for-leak-kinds=definite \
    --error-exitcode=99 -q ./build/fuzz_leak 20000

for t in build/unit_test_* build/component_test_smoke build/component_test_files; do
    if [ -x "$t" ]; then
        valgrind --leak-check=full --errors-for-leak-kinds=definite \
            --error-exitcode=99 -q "$t" >/dev/null
    fi
done

printf 'valgrind: no errors or definite leaks\n'
