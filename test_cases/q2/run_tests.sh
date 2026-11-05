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
ALL_CASES="case0_format case6_entry_limit case7_boundaries case8_deep_paths case2_paths case5_invalid case9_file_boundaries case1_basic case3_blocks case4_persist case10_append case11_disk_full case12_corrupt case13_scattered case14_format_fail"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
failures=0

# rep CHAR N: print CHAR repeated N times, no newline
rep() { head -c "$2" /dev/zero | tr '\0' "$1"; }
# fresh_dir NAME: print the path of a new, empty working directory
fresh_dir() { rm -rf "${WORK:?}/$1"; mkdir -p "$WORK/$1"; printf '%s' "$WORK/$1"; }
# normalise: drop the start-up banner and command list, strip "minifs> " prompts
normalise() { sed -e '/^Mini File System Simulator$/,/^  exit$/d' -e 's/minifs> //g'; }
# exit_note STATUS: name a non-zero minifs exit status, so the output diff fails
exit_note() { [ "$1" -eq 0 ] || echo "minifs exited with status $1"; }
# run_session DIR FILE: run minifs in DIR with FILE on stdin; print normalised output
run_session() {
    (cd "$1" && "$BIN" < "$2" 2>&1; exit_note $?) | normalise
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
ls
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
    session "$d" $'format\nexit\n' > /dev/null
    poke "$d" 8484 '\001\000\000\000' # root size = 1
    out+=$'\n--- bad root ---\n'$(session "$d" $'ls\nformat\nls\nexit\n')
    session "$d" $'format\ncreate a.txt\nwrite a.txt "'"$(rep A 300)"$'"\nexit\n' > /dev/null
    poke "$d" 388 '\101\000'          # fat[66] -> block 65: a cycle
    out+=$'\n--- FAT cycle ---\n'$(session "$d" $'ls\nformat\nls\nexit\n')
    session "$d" $'format\ncreate a.txt\nwrite a.txt "x"\nexit\n' > /dev/null
    poke "$d" 8548 '\054\001\000\000' # entries[1].size = 300 (1 block)
    out+=$'\n--- size mismatch ---\n'$(session "$d" $'ls\nformat\nls\nexit\n')
    session "$d" $'format\ncreate a.txt\nexit\n' > /dev/null
    poke "$d" 8512 "$(rep N 32)"      # entries[1].name fills all 32 bytes
    out+=$'\n--- unterminated name ---\n'$(session "$d" $'ls\nformat\nls\nexit\n')
    check case12_corrupt "$out" "$d"
}

case13_scattered() {
    local d out x1 x2 x3; d=$(fresh_dir case13_scattered)
    x1=$(rep 1 256); x2=$(rep 2 256); x3=$(rep 3 10)
    out=$(session "$d" "format
create x
create y
append x \"$x1\"
append y \"$(rep Y 256)\"
append x \"$x2\"
append y \"$(rep Y 256)\"
append x \"$x3\"
read x
stat x
delete y
create z
write z \"$(rep Z 600)\"
read z
read x
df
exit
")
    out+=$'\n--- restart ---\n'
    out+=$(session "$d" $'read x\nread z\ndf\nexit\n')
    check case13_scattered "$out" "$d"
}

case14_format_fail() {
    local d out pid status i fifo="$WORK/fifo14" log="$WORK/out14"
    d=$(fresh_dir case14_format_fail)
    rm -f "$fifo"; mkfifo "$fifo"
    (cd "$d" && "$BIN" < "$fifo" > "$log" 2>&1) & pid=$!
    exec 3> "$fifo"
    printf 'format\ncreate a.txt\nwrite a.txt "kept"\npwd\n' >&3
    # Wait for pwd's output, then make the disk path impossible to (re)create.
    for i in $(seq 200); do grep -q '> /$' "$log" && break; sleep 0.05; done
    mv "$d/virtual_disk.bin" "$WORK/disk14.bin"; mkdir "$d/virtual_disk.bin"
    printf 'format\nread a.txt\nls\nexit\n' >&3
    exec 3>&-
    wait "$pid"; status=$?
    rmdir "$d/virtual_disk.bin"; mv "$WORK/disk14.bin" "$d/virtual_disk.bin"
    out=$({ cat "$log"; exit_note "$status"; } | normalise)
    out+=$'\n--- restart ---\n'
    out+=$(session "$d" $'read a.txt\nexit\n')
    check case14_format_fail "$out" "$d"
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
