# Q2 Mini File System (`minifs`) — Design

Date: 2026-11-05
Scope: complete `problem2.c` so `./minifs` meets CS3103 Problem 2, plus the
boundary tests the specification requires and the README disk-layout section.

## 1. Goals and constraints

- Implement all 14 commands (`format create write append read delete mkdir
  rmdir cd pwd ls stat df exit`) with the semantics in the specification.
- Store the entire simulated file system inside `virtual_disk.bin`
  (exactly 1,048,576 bytes). No host file per simulated file, no sidecar
  metadata, no file-system library.
- Persistent across restart; failed or invalid commands never crash and never
  leave partial changes on disk or in memory.
- C11, single source file `problem2.c` (already built by `Makefile` and
  `CMakeLists.txt`). The starter's command parser and command semantics are
  kept. Tested on Linux (Docker image `cs3103-clion-linux:1.0`, Ubuntu 24.04).
- Code-quality rubric: every written or modified function gets a header
  comment; named constants for capacities and marker values; `main` only
  orchestrates.

Out of scope: crash consistency (journaling), file permissions, timestamps,
hard/soft links, multi-user access.

## 2. Disk layout

Block size 256 B, 4096 blocks. Blocks 0–64 are metadata and are permanently
marked `RESERVED` in the FAT.

| Blocks   | Region      | Size        | Contents |
|----------|-------------|-------------|----------|
| 0        | Superblock  | 1 block     | magic, version, geometry and region offsets |
| 1–32     | FAT         | 32 blocks   | `uint16_t fat[4096]` (8,192 B) |
| 33–64    | Entry table | 32 blocks   | 128 × 64-byte `file_entry_t` (8,192 B) |
| 65–4095  | Data        | 4031 blocks | file contents (1,031,936 B) |

Structures are written in the host's native byte order (little-endian on the
x86-64/ARM64 Linux targets); fixed-width integer types are used throughout
and `_Static_assert` pins every on-disk size.

### 2.1 Superblock (block 0, zero-padded to 256 B)

`magic` = `0x464E494D` ("MINF"), `version` = 1, `block_size` = 256,
`total_blocks` = 4096, `fat_start` = 1, `fat_blocks` = 32,
`entry_start` = 33, `entry_blocks` = 32, `data_start` = 65,
`max_entries` = 128, `entry_size` = 64.

### 2.2 FAT (blocks 1–32)

One `uint16_t` per disk block:

| Value            | Meaning |
|------------------|---------|
| `0x0000` FREE    | data block available |
| `0xFFFF` END     | last block of a file's chain |
| `0xFFFE` RESERVED| metadata block (0–64), never allocatable |
| 65–4095          | index of the next block in the same file |

The FAT is both the allocation map and the free-space map; there is no
separate bitmap.

### 2.3 Entry table (blocks 33–64)

`file_entry_t`, exactly 64 bytes:

| Field         | Type        | Meaning |
|---------------|-------------|---------|
| `name`        | `char[32]`  | one path component, NUL-terminated (≤ 31 chars) |
| `parent_id`   | `int32_t`   | entry-table index of the parent directory |
| `size`        | `uint32_t`  | byte size (regular files; 0 for directories) |
| `first_block` | `uint16_t`  | first data block, or END when the file has none |
| `type`        | `uint8_t`   | 0 unused, 1 `TYPE_FILE`, 2 `TYPE_DIR` |
| padding       | `uint8_t[17]` | zero |

Entry 0 is the root: name `/`, type dir, `parent_id` 0. Directories own no
data blocks; a directory's children are the live entries whose `parent_id`
equals its index. With 128 entries a linear scan is the lookup mechanism.

## 3. In-memory state

`filesystem_t` holds: `FILE *disk`, `int mounted`, the superblock, the full
FAT, the full entry table, and `int cwd_id`. `cwd_id` replaces the starter's
`cwd` string; `pwd` rebuilds the absolute path by following `parent_id` to
the root.

Reads of file contents go to the disk file block by block; metadata is
served from memory.

## 4. Startup, format and persistence

**Startup (`open_virtual_disk`)**

- Disk file missing → message "No file system found; run 'format'." The
  program stays unmounted; every command except `format` and `exit` reports
  "file system is not formatted".
- Disk file present → verify file size == 1,048,576, superblock magic,
  version and every geometry field, then validate metadata:
  - root entry is a directory at index 0 with parent 0;
  - every live entry has a valid type, a NUL-terminated valid name, and a
    parent that is a live directory; no duplicate names in one directory;
    following parents from any entry reaches the root (no cycles);
  - FAT blocks 0–64 are RESERVED; every data-block value is FREE, END or a
    data-block index;
  - each file chain is walked at most 4096 steps, uses no block twice and no
    block owned by another file, and has length ⌈size / 256⌉; directories
    have `first_block` = END and size 0;
  - no data block (65–4095) is non-FREE without belonging to a file chain.
  Any failure → "virtual_disk.bin is not a valid minifs image; run 'format'."
  and the program stays unmounted. It never formats automatically.

**`format`** opens the disk with `w+b` (create or truncate), writes 1 MiB of
zeros, writes the superblock, a FAT with blocks 0–64 RESERVED and the rest
FREE, and an entry table containing only the root; flushes; mounts; sets
`cwd_id` = 0. Prints a one-line confirmation.

**Persistence.** After every successful mutating command the order is:
write the affected data blocks, then write superblock + FAT + entry table
(blocks 0–64), then `fflush`. `exit` and EOF close the disk file.

## 5. Block allocation and file operations

**Allocator.** Scan the FAT from block 65 upward and take the lowest FREE
block. A running `free_blocks` count is derived from the FAT at mount time
and maintained on every allocate/free. Freed blocks are therefore reused
first, which makes block reuse observable in tests.

**Helpers.** `chain_length(first)`, `chain_block_at(first, k)`,
`chain_last(first)`, `chain_free_from(first)`, `chain_extend(entry, n)`.
All chain walks are bounded by `NUM_BLOCKS` steps.

| Command  | Behaviour |
|----------|-----------|
| `create` | resolve parent; validate name and uniqueness; take a free entry slot; `size` 0, `first_block` END; no data block allocated |
| `write`  | target must be an existing regular file. `need` = ⌈len/256⌉, `have` = chain length. If `need > have` and `need − have > free_blocks` → fail with no change. Otherwise extend the chain (grow) or cut it after block `need` and free the remainder (shrink); write the content block by block; set `size` = len |
| `append` | target must be an existing regular file. If `size % 256 ≠ 0` the free tail of the last block is filled first. Extra blocks = ⌈(size+len)/256⌉ − ⌈size/256⌉; checked against `free_blocks` before any change; then link new blocks and write the rest |
| `read`   | target must be a regular file; walk the chain, emit `min(256, remaining)` bytes per block, then one newline. An empty file prints an empty line |
| `delete` | target must be a regular file (a directory → error suggesting `rmdir`); free the whole chain; clear the entry |

**No partial updates.**

1. Every check (path, type, name, duplicate, entry slot, free space) runs
   before any state changes.
2. Mutating commands snapshot the in-memory FAT, entry table and free count
   (≈16 KB `memcpy`) before mutating.
3. If a host I/O call fails during the operation, the snapshot is restored,
   the metadata region is rewritten from the restored state, and an error is
   printed.

Insufficient space, a full entry table, invalid names and invalid paths are
all rejected in step 1, so they never modify anything.

## 6. Directories and path resolution

**`resolve_path(fs, path, &id)`** — used by every command that takes a path.
Absolute paths start at root (0), relative paths at `cwd_id`. The path is
split on `/`; empty components are ignored (`a//b/` ≡ `a/b`). `.` keeps the
current entry, `..` moves to its parent (root's parent is root). Any other
component is looked up among the current directory's children; every
intermediate component must exist and be a directory. Errors: "No such file
or directory", "Not a directory", "Invalid path" (empty string).

**`resolve_parent(fs, path, &parent_id, leaf)`** — for `create` and `mkdir`.
Resolves everything except the last non-empty component, which becomes the
new name. The leaf must pass the name rules; a path with no leaf (`/`) or a
leaf of `.`/`..` is invalid.

**Name rules.** 1–31 characters, printable ASCII (0x21–0x7E; whitespace
cannot occur because the tokenizer splits on it), no `/`, not `.` or `..`.
A duplicate name in the same directory is rejected. Creating any entry when
all 128 slots are used (root included) is rejected.

| Command | Behaviour |
|---------|-----------|
| `mkdir` | like `create`, type dir, no blocks |
| `rmdir` | target must be a directory, not `/`, not the current directory, and empty |
| `cd`    | target must be a directory; sets `cwd_id` |
| `pwd`   | prints the absolute path built from `parent_id` links (`/` for root) |
| `ls`    | lists the current directory's children sorted by name in byte order (`strcmp`); `.`/`..` not listed; an empty directory prints only the header |
| `stat`  | file: name, type, size in bytes, data-block count. Directory: name, type, direct-child count (excluding `.`/`..`) |
| `df`    | block size, total, used (reserved metadata + data), free, metadata reserved; used + free = 4096 |

## 7. Output conventions

- Successful commands print nothing, except `format` (confirmation) and the
  commands whose purpose is output (`read pwd ls stat df`).
- Errors go to stderr as `Error: <command> <path>: <reason>`.
- The starter banner line "Student starter: file-system operations are
  TODOs." is removed.

`ls`:

```
NAME                             TYPE   SIZE
courses                          dir       -
hello.txt                        file     17
```

`stat` (file / directory):

```
Name:   hello.txt          Name:     os
Type:   file               Type:     directory
Size:   17 bytes           Children: 1
Blocks: 1
```

`df`:

```
Block size:         256 bytes
Total blocks:       4096
Used blocks:        67 (metadata 65 + data 2)
Free blocks:        4029
Metadata reserved:  65
```

## 8. Invariants

1. Blocks 0–64 are RESERVED in the FAT; no chain contains them.
2. Every data block (65–4095) is FREE or belongs to exactly one file chain.
3. For every regular file, chain length = ⌈size / 256⌉, and an empty file has
   `first_block` = END.
4. Directories own no data blocks.
5. used + free = 4096, where used = 65 reserved + allocated data blocks.
6. Every live entry other than root has a parent that is a live directory,
   and following parents always reaches root.
7. Names are unique within each directory.
8. Root (entry 0) exists after `format`, is a directory, and is its own
   parent.
9. At most 128 live entries, root included.

## 9. Capacities

| Item | Limit |
|------|-------|
| Entries (files + directories) | 128 including root |
| Name length | 31 characters per component |
| Data blocks | 4031 |
| Largest single file | 4031 × 256 = 1,031,936 bytes |
| Command line length | unbounded (`getline`) |

## 10. Testing

**Runner `test_cases/q2/run_tests.sh`** (run on Linux / in the Docker image):

1. `make minifs`.
2. Each case runs in a fresh temporary directory with stdout and stderr
   merged; the output is diffed against `test_cases/q2/expected/<case>.out`.
3. After each case, assert `virtual_disk.bin` is exactly 1,048,576 bytes.
4. The two halves of case4 run in the same directory, in order.
5. Print PASS/FAIL per case; exit non-zero on any failure.

Expected files are generated after implementation and checked by hand
against `test_cases/q2/README.txt` and the specification before being
committed (case1 three reads, case2 `pwd` values, case3 sizes
0/256/257/600/5 and reuse, case6 failure of `f127.txt`).

**Existing cases:** case1_basic, case2_paths, case3_blocks,
case4_persist_create + case4_persist_restart, case5_invalid,
case6_entry_limit.

**New cases:**

- `case7_boundaries.txt`: 31-char name accepted, 32-char rejected; `.`,
  `..` and `/` rejected as new names; `cd ..` at `/` stays at `/`; `cd`
  into a file fails; `rmdir .` and `rmdir` of the current directory fail;
  `delete` on a directory fails; `read` on a directory fails; `write` to a
  missing file fails; empty file `read` and `stat`; `df` unchanged across
  the failures.
- `case8_disk_full` (commands generated by the runner with `awk`, because
  the content is about 1 MB):
  1. `keep.txt` ← "keep me" (1 block); `big.txt` ← 4029 × 256 bytes.
     Free = 1.
  2. `write new.txt` with 600 bytes (3 blocks) fails; `new.txt` stays 0
     bytes / 0 blocks.
  3. `write keep.txt` with 600 bytes (needs 2 extra) fails; `keep.txt`
     still reads "keep me".
  4. `append big.txt` with 257 bytes (2 blocks) fails; `stat big.txt`
     unchanged.
  5. `append big.txt` with 256 bytes succeeds; free = 0; a 1-byte `write
     new.txt` fails.
  6. `delete big.txt`; `write new.txt` 600 bytes succeeds.
  7. Restart: `read keep.txt`, `stat new.txt`, `df` match the
     pre-restart state.
- `case9_corrupt` (runner-driven): start `minifs` with (a) a truncated
  `virtual_disk.bin` and (b) a disk with a bad magic; each must report an
  invalid image, reject `ls`, and not crash; `format` then succeeds and
  the disk is 1 MiB again.

## 11. Documentation

- `README.md`: Problem 2 section with the layout table, entry format, FAT
  values, capacities, and how to run `run_tests.sh`.
- `test_cases/q2/README.txt`: short note describing case7–case9 and the
  runner.

## 12. Code organisation (`problem2.c`)

Sections in order: constants and on-disk structures → disk I/O
(`read_block`, `write_block`, `flush_metadata`, load + validate) → FAT
allocation and chain helpers → entry-table helpers (find child, free slot,
name rules) → path resolution → command implementations (`fs_*`) → command
interpreter (starter) → `main`. Every written or modified function carries a
header comment stating purpose, inputs and return value or side effects.
