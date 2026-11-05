
## Development Environment

- Language: C11 / GNU C11
- OS: Ubuntu 24.04
- IDE: CLion
- Compiler: GCC
- Build System: CMake
- Debugger: GDB

## Setup

1. Clone the GitHub repository.
2. Install Docker Desktop.
3. Build the development image:

   docker build -t cs3103-clion-linux:1.0 -f Dockerfile.clion .

4. Open the project in CLion.
5. Configure a Docker Toolchain using the image.
6. Create a CMake Profile called Docker-Debug.
7. Use the Docker-Debug profile to build and run.

## Targets

- problem1: Multi-Stage Producer-Consumer
- minifs: Mini File System Simulator
- problem3: CPU Scheduling

## Build on Linux

```bash
make
```

## Run

### Problem 1
```bash
./problem1
```

### Problem 2
```bash
./minifs
```

### Problem 3
```bash
./problem3 test_cases/q3/case1.txt
```

## Problem 2 — minifs disk layout

`./minifs` keeps the whole file system in one host file, `virtual_disk.bin`
(exactly 1,048,576 bytes): 4096 blocks of 256 bytes. Structures use the host's
native byte order (little-endian on the Linux targets).

| Blocks   | Region      | Size        | Contents |
|----------|-------------|-------------|----------|
| 0        | Superblock  | 1 block     | magic, version, geometry and region offsets |
| 1–32     | FAT         | 32 blocks   | `uint16_t fat[4096]` (8,192 B) |
| 33–64    | Entry table | 32 blocks   | 128 × 64-byte `file_entry_t` (8,192 B) |
| 65–4095  | Data        | 4031 blocks | file contents (1,031,936 B) |

Blocks 0–64 (65 metadata blocks) are permanently marked reserved in the FAT.

`file_entry_t` (64 bytes; the entry's index in the table is its ID, entry 0 is
the root, named `/`, its own parent):

| Field         | Type          | Meaning |
|---------------|---------------|---------|
| `name`        | `char[32]`    | one path component, NUL-terminated (at most 31 characters) |
| `parent_id`   | `int32_t`     | entry-table index of the parent directory |
| `size`        | `uint32_t`    | byte size (files; 0 for directories) |
| `first_block` | `uint16_t`    | first data block, or `0xFFFF` if the file has none |
| `type`        | `uint8_t`     | 0 unused, 1 file, 2 directory |
| `reserved`    | `uint8_t[21]` | zero padding |

FAT values (one `uint16_t` per disk block; the FAT is also the free-space map):

| Value     | Meaning |
|-----------|---------|
| `0x0000`  | free data block |
| `0xFFFF`  | last block of a file's chain |
| `0xFFFE`  | reserved metadata block (0–64), never allocated |
| 65–4095   | index of the next block of the same file |

Capacities:

| Item | Limit |
|------|-------|
| Entries (files + directories) | 128, including the root |
| Name length | 31 characters per path component |
| Data blocks | 4031 |
| Largest single file | 4031 × 256 = 1,031,936 bytes |

On start-up the image is validated (size, superblock, entry table, FAT and
every file chain); a damaged image is rejected with a message and is never
formatted automatically.

### Running the Problem 2 tests

Tests run on Linux, from the repository root:

```bash
# all cases
docker run --rm -v "$PWD":/workspace -w /workspace cs3103-clion-linux:1.0 bash test_cases/q2/run_tests.sh
# selected cases
docker run --rm -v "$PWD":/workspace -w /workspace cs3103-clion-linux:1.0 bash test_cases/q2/run_tests.sh case11_disk_full case12_corrupt
```
