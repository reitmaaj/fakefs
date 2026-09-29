#!/bin/sh -eu

set -eu

cd "$(dirname "$0")/.."

for t in test/unit/test_*.c; do
    name="$(basename "$t" .c)"
    cc -std=c89 -pedantic -Wall -Wextra -Werror -Wno-long-long \
        -Iinclude -Isrc -Itest -Itest/unit --coverage \
        "$t" build/libfakefs_cov.a -lsqlite3 -o "build/cov_unit_$name"
    "build/cov_unit_$name" >/dev/null
done

for t in test/component/test_*.c test/integration/test_*.c; do
    name="$(basename "$t" .c)"
    cc -std=c89 -pedantic -Wall -Wextra -Werror -Wno-long-long -D_POSIX_C_SOURCE=200112L \
        -Iinclude -Itest --coverage \
        "$t" build/libfakefs_cov.a -lsqlite3 -o "build/cov_$name"
    "build/cov_$name" >/dev/null
done

lcov --capture --directory build --output-file build/coverage.info \
    --rc lcov_branch_coverage=1 >/dev/null 2>&1 || true
lcov --remove build/coverage.info '/usr/*' '*/test/*' \
    --output-file build/coverage_filtered.info \
    --rc lcov_branch_coverage=1 >/dev/null 2>&1 || true

summary="$(lcov --summary build/coverage_filtered.info 2>/dev/null || true)"
printf '%s\n' "$summary"

line_cov="$(printf '%s\n' "$summary" | awk -F'[:% ]+' '/lines/{for(i=1;i<=NF;i++) if($i ~ /^[0-9.]+$/){print $i; exit}}')"
if [ -n "$line_cov" ] && awk -v c="$line_cov" 'BEGIN{exit !(c < 90.0)}'; then
    printf 'coverage below 90%% threshold (%s%%)\n' "$line_cov" >&2
    exit 1
fi

fn_cov="$(printf '%s\n' "$summary" | awk -F'[:% ]+' '/functions/{for(i=1;i<=NF;i++) if($i ~ /^[0-9.]+$/){print $i; exit}}')"
if [ -n "$fn_cov" ] && awk -v c="$fn_cov" 'BEGIN{exit !(c < 90.0)}'; then
    printf 'function coverage below 90%% threshold (%s%%)\n' "$fn_cov" >&2
    exit 1
fi
