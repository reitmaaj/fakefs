#!/bin/sh -eu

cd "$(dirname "$0")/.."

scope="${1:-src/}"
threshold_file="${2:-test/complexity_threshold.txt}"
lizard="${LIZARD:-lizard}"

if ! command -v "$lizard" >/dev/null 2>&1; then
    printf 'lizard not installed; cannot run function-size gate\n' >&2
    exit 1
fi

if [ ! -f "$threshold_file" ]; then
    printf 'missing threshold file: %s\n' "$threshold_file" >&2
    exit 1
fi

threshold="$(tr -d '[:space:]' < "$threshold_file")"

set +e
csv="$("$lizard" "$scope" --csv 2>/dev/null)"
status=$?
set -e

if [ "$status" -ne 0 ]; then
    printf 'lizard failed on %s (status %s)\n' "$scope" "$status" >&2
    exit 1
fi

summary="$(printf '%s\n' "$csv" | awk -F, '
    NR > 1 { total++; if ($1 + 0 < 10) small++ }
    END {
        if (total == 0) { print "0.0 0"; exit }
        printf "%.1f %d", 100.0 * small / total, total
    }')"

ratio="${summary%% *}"
count="${summary##* }"

if [ "$count" -eq 0 ]; then
    printf 'no functions found in scope %s\n' "$scope" >&2
    exit 1
fi

if ! awk -v r="$ratio" -v t="$threshold" \
    'BEGIN { exit !(r + 0 >= t + 0) }'; then
    printf 'function-size ratio %s%% below threshold %s%% (scope %s)\n' \
        "$ratio" "$threshold" "$scope" >&2
    exit 1
fi

printf 'function-size ratio %s%% (threshold %s%%, scope %s)\n' \
    "$ratio" "$threshold" "$scope"
