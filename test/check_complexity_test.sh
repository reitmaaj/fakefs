#!/bin/sh -eu

cd "$(dirname "$0")/.."

gate="test/check_complexity.sh"
checks=0

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail()
{
    printf 'FAIL: %s\n' "$1" >&2
    exit 1
}

make_tree()
{
    dir="$1"
    small="$2"
    large="$3"
    mkdir -p "$dir"
    i=0
    while [ "$i" -lt "$small" ]; do
        printf 'static int small_%d(int x)\n{\n    return x + %d;\n}\n' \
            "$i" "$i"
        i=$((i + 1))
    done > "$dir/small.c"
    i=0
    while [ "$i" -lt "$large" ]; do
        printf 'static int large_%d(int x)\n{\n    if (x == 0)\n    {\n        return 1;\n    }\n    if (x == 1)\n    {\n        return 2;\n    }\n    return x;\n}\n' \
            "$i"
        i=$((i + 1))
    done > "$dir/large.c"
}

expect_status()
{
    want="$1"
    label="$2"
    shift 2
    set +e
    output="$("$@" 2>&1)"
    status=$?
    set -e
    if [ "$status" -ne "$want" ]; then
        printf 'FAIL: %s: expected status %s, got %s\n%s\n' \
            "$label" "$want" "$status" "$output" >&2
        exit 1
    fi
    checks=$((checks + 1))
}

expect_contains()
{
    needle="$1"
    case "$output" in
    *"$needle"*)
        checks=$((checks + 1))
        ;;
    *)
        printf 'FAIL: output missing "%s":\n%s\n' "$needle" "$output" >&2
        exit 1
        ;;
    esac
}

make_tree "$tmp/good" 10 0
make_tree "$tmp/bad" 4 6
make_tree "$tmp/empty" 0 0
printf '90.0\n' > "$tmp/thr90.txt"

expect_status 0 "good tree passes" \
    sh "$gate" "$tmp/good" "$tmp/thr90.txt"
expect_contains "100.0"

expect_status 1 "bad tree fails" \
    sh "$gate" "$tmp/bad" "$tmp/thr90.txt"
expect_contains "below threshold"

expect_status 1 "missing lizard fails" \
    env LIZARD="$tmp/no-such-lizard" \
    sh "$gate" "$tmp/good" "$tmp/thr90.txt"
expect_contains "lizard not installed"

expect_status 1 "empty scope fails" \
    sh "$gate" "$tmp/empty" "$tmp/thr90.txt"
expect_contains "no functions"

expect_status 1 "missing threshold fails" \
    sh "$gate" "$tmp/good" "$tmp/no-such-threshold.txt"
expect_contains "missing threshold"

printf 'check_complexity_test: %d checks ok\n' "$checks"
