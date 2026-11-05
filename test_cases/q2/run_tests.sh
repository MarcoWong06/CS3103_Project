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
ALL_CASES="case0_format case6_entry_limit case7_boundaries case8_deep_paths case2_paths case5_invalid case9_file_boundaries case1_basic case3_blocks case4_persist case10_append case11_disk_full case12_corrupt"
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
case8_deep_paths() {
    local d n cmds=$'format\n' abs="" i
    d=$(fresh_dir case8_deep_paths); n=$(rep d 31)
    for i in $(seq 127); do cmds+="mkdir $n"$'\n'"cd $n"$'\n'; abs+="/$n"; done
    cmds+=$'pwd\n'"mkdir $n"$'\ncd /\n'"stat $abs"$'\nexit\n'
    check case8_deep_paths "$(session "$d" "$cmds")" "$d"
}
case4_persist() {
    local d out; d=$(fresh_dir case4_persist)
    out=$(run_session "$d" "$Q2/case4_persist_create.txt")
    out+=$'\n--- restart ---\n'
    out+=$(run_session "$d" "$Q2/case4_persist_restart.txt")
    check case4_persist "$out" "$d"
}
case11_disk_full() {
    local d out; d=$(fresh_dir case11_disk_full)
    out=$(session "$d" "format
create keep.txt
write keep.txt \"keep me\"
create big.txt
write big.txt \"$(rep A 1031424)\"
create new.txt
df
write new.txt \"$(rep N 600)\"
stat new.txt
write keep.txt \"$(rep K 600)\"
read keep.txt
append big.txt \"$(rep B 257)\"
stat big.txt
append big.txt \"$(rep B 256)\"
df
write new.txt \"x\"
delete big.txt
write new.txt \"$(rep N 1031937)\"
write new.txt \"$(rep N 600)\"
stat new.txt
exit
")
    out+=$'\n--- restart ---\n'
    out+=$(session "$d" $'read keep.txt\nstat new.txt\ndf\nexit\n')
    check case11_disk_full "$out" "$d"
}

case12_corrupt() {
    local d out; d=$(fresh_dir case12_corrupt)
    session "$d" $'format\nexit\n' > /dev/null
    truncate -s 1000 "$d/virtual_disk.bin"
    out=$'--- truncated ---\n'$(session "$d" $'ls\nformat\nexit\n')
    session "$d" $'format\nexit\n' > /dev/null; poke "$d" 0 'XXXX'
    out+=$'\n--- bad magic ---\n'$(session "$d" $'ls\nexit\n')
    session "$d" $'format\ncreate a.txt\nwrite a.txt "x"\nexit\n' > /dev/null
    poke "$d" 386 '\001\000'          # fat[65] -> block 1 (metadata)
    out+=$'\n--- bad FAT ---\n'$(session "$d" $'read a.txt\nexit\n')
    session "$d" $'format\ncreate a.txt\nexit\n' > /dev/null
    poke "$d" 8544 '\310\000\000\000' # entries[1].parent_id = 200
    out+=$'\n--- bad parent ---\n'$(session "$d" $'ls\nformat\nls\nexit\n')
    check case12_corrupt "$out" "$d"
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
