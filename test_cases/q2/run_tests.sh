#!/usr/bin/env bash
# Q2 minifs regression tests (Linux). Usage: test_cases/q2/run_tests.sh [case ...]
# A case is a function below or a script test_cases/q2/<case>.txt run in a
# fresh directory. Output (banner and "minifs> " prompts removed) is diffed
# against test_cases/q2/expected/<case>.out; the disk must be exactly 1 MiB.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
Q2="$ROOT/test_cases/q2"
BIN="$ROOT/minifs"
DISK_BYTES=1048576
ALL_CASES="case0_format"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
failures=0

# rep CHAR N: print CHAR repeated N times, no newline
rep() { head -c "$2" /dev/zero | tr '\0' "$1"; }
# fresh_dir NAME: print the path of a new, empty working directory
fresh_dir() { rm -rf "${WORK:?}/$1"; mkdir -p "$WORK/$1"; printf '%s' "$WORK/$1"; }
# run_session DIR FILE: run minifs in DIR with FILE on stdin; print normalised output
run_session() {
    (cd "$1" && "$BIN" < "$2" 2>&1) |
        sed -e '/^Mini File System Simulator$/,/^  exit$/d' -e 's/minifs> //g'
}
# session DIR TEXT: run_session with the commands given as a string
session() { printf '%s' "$2" > "$WORK/cmds.txt"; run_session "$1" "$WORK/cmds.txt"; }
# poke DIR OFFSET ESCAPES: overwrite bytes (printf escapes) at OFFSET of DIR's disk
poke() { printf "$3" | dd of="$1/virtual_disk.bin" bs=1 seek="$2" conv=notrunc status=none; }
# check NAME OUTPUT DIR: diff OUTPUT with expected/NAME.out; DIR's disk must be 1 MiB
check() {
    local ok=1 size
    diff -u "$Q2/expected/$1.out" <(printf '%s\n' "$2") > "$WORK/diff" || ok=0
    size=$(wc -c < "$3/virtual_disk.bin" 2>/dev/null || echo 0)
    [ "$size" -eq "$DISK_BYTES" ] || { echo "disk is $size bytes" >> "$WORK/diff"; ok=0; }
    if [ "$ok" -eq 1 ]; then echo "PASS $1"; else echo "FAIL $1"; cat "$WORK/diff"; failures=$((failures + 1)); fi
}
# script_case NAME: run test_cases/q2/NAME.txt in a fresh directory
script_case() { local d; d=$(fresh_dir "$1"); check "$1" "$(run_session "$d" "$Q2/$1.txt")" "$d"; }

# ---- runner-generated cases ----
case0_format() {
    local d out
    d=$(fresh_dir case0_format)
    out=$(session "$d" $'df\npwd\nls\ncreate a.txt\nread a.txt\nstat /\nformat\ndf\nexit\n')
    out+=$'\n--- restart ---\n'
    out+=$(session "$d" $'df\nformat\ndf\nexit\n')
    check case0_format "$out" "$d"
}

# ---- main ----
warnings=$(gcc -std=c11 -Wall -Wextra -pedantic -fsyntax-only "$ROOT/problem2.c" 2>&1)
[ -z "$warnings" ] || { echo "FAIL build: warnings"; echo "$warnings"; exit 1; }
make -s -B -C "$ROOT" minifs || { echo "FAIL build"; exit 1; }
for c in ${*:-$ALL_CASES}; do
    if declare -F "$c" > /dev/null; then "$c"
    elif [ -f "$Q2/$c.txt" ]; then script_case "$c"
    else echo "FAIL $c (unknown case)"; failures=$((failures + 1)); fi
done
echo "$failures failure(s)"
[ "$failures" -eq 0 ]
