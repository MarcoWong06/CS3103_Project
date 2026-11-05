/*
 * CS3103 Operating Systems - Problem 2 Starter Code
 * Mini File System Simulator
 *
 * This file intentionally provides only a framework.
 * Students must implement the file-system operations marked TODO.
 */

/* Build on Linux: gcc -std=c11 -Wall -Wextra problem2.c -o minifs */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <ctype.h>
#include <errno.h>

#define DISK_FILE       "virtual_disk.bin"
#define DISK_SIZE       (1024 * 1024)
#define BLOCK_SIZE      256
#define NUM_BLOCKS      (DISK_SIZE / BLOCK_SIZE)
#define MAX_ENTRIES     128
#define MAX_NAME_LEN    31
/* Initial storage choice, not an assignment limit; extend as needed. */
#define MAX_PATH_LEN    256

#define TYPE_UNUSED     0
#define TYPE_FILE       1
#define TYPE_DIR        2

/*
 * On-disk layout (256-byte blocks, 4096 blocks, little-endian host):
 *   block 0        superblock (zero padded)
 *   blocks 1-32    FAT: one uint16_t per block
 *   blocks 33-64   entry table: 128 entries of 64 bytes
 *   blocks 65-4095 data
 */
#define FS_MAGIC        0x464E494Du
#define FS_VERSION      1u
#define FAT_START       1
#define FAT_BLOCKS      32
#define ENTRY_START     33
#define ENTRY_BLOCKS    32
#define DATA_START      65
#define ENTRY_SIZE      64

#define FAT_FREE        0x0000
#define FAT_END         0xFFFF
#define FAT_RESERVED    0xFFFE
#define ROOT_ID         0

/* Message catalogue: error reasons and fixed startup / format lines. */
#define MSG_IO          "disk I/O error"
#define MSG_UNMOUNTED   "no valid file system; run 'format'"
#define MSG_NO_FS       "No file system found; run 'format'."
#define MSG_BAD_IMAGE   "Error: " DISK_FILE " is not a valid minifs image; run 'format'"
#define MSG_FORMAT_OK   "Formatted " DISK_FILE ": 4096 blocks x 256 bytes (65 metadata, 4031 data)"

/*
 * One metadata-table entry (exactly ENTRY_SIZE bytes on disk). The entry's
 * index in the table is its ID; the root is entry ROOT_ID and is its own parent.
 */
typedef struct {
    char name[MAX_NAME_LEN + 1]; /* One name, not the full path. */
    int32_t parent_id;          /* Parent directory's metadata-table index. */
    uint32_t size;              /* File size in bytes (0 for directories). */
    uint16_t first_block;       /* First FAT chain block, or FAT_END if none. */
    uint8_t type;               /* TYPE_UNUSED, TYPE_FILE or TYPE_DIR. */
    uint8_t reserved[21];       /* Pads the entry to ENTRY_SIZE bytes. */
} file_entry_t;

/* Superblock stored in block 0; every field must match its constant. */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t fat_start;
    uint32_t fat_blocks;
    uint32_t entry_start;
    uint32_t entry_blocks;
    uint32_t data_start;
    uint32_t max_entries;
    uint32_t entry_size;
} superblock_t;

/* File-system state used while the simulator is running. */
typedef struct {
    FILE *disk;
    int mounted;                      /* 1 when the disk holds a valid image. */
    superblock_t superblock;
    uint16_t fat[NUM_BLOCKS];         /* In-memory copy of the FAT. */
    file_entry_t entries[MAX_ENTRIES];/* In-memory copy of the entry table. */
    uint32_t free_blocks;             /* Number of free data blocks. */
    char cwd[MAX_PATH_LEN];           /* Replaced by a directory ID later. */
} filesystem_t;

_Static_assert(FAT_BLOCKS * BLOCK_SIZE == NUM_BLOCKS * sizeof(uint16_t),
               "FAT must exactly cover all blocks");
_Static_assert(ENTRY_BLOCKS * BLOCK_SIZE == MAX_ENTRIES * ENTRY_SIZE,
               "entry table must exactly fill its blocks");
_Static_assert(sizeof(file_entry_t) == ENTRY_SIZE, "file_entry_t must be 64 bytes");
_Static_assert(sizeof(superblock_t) <= BLOCK_SIZE, "superblock must fit in one block");

/* ---------- Utility functions provided in the starter ---------- */

static void trim_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        --n;
    }
}

static char *skip_spaces(char *s)
{
    while (*s != '\0' && isspace((unsigned char)*s)) {
        ++s;
    }
    return s;
}

static char *next_token(char **cursor)
{
    char *start = skip_spaces(*cursor);
    char *end;

    if (*start == '\0') {
        *cursor = start;
        return NULL;
    }

    end = start;
    while (*end != '\0' && !isspace((unsigned char)*end)) {
        ++end;
    }

    if (*end != '\0') {
        *end = '\0';
        *cursor = end + 1;
    } else {
        *cursor = end;
    }

    return start;
}

/* Display the assignment's commands at startup; no help command is required. */
static void print_commands(void)
{
    printf("Commands:\n");
    printf("  format\n");
    printf("  create <path>\n");
    printf("  write <path> \"<content>\"\n");
    printf("  append <path> \"<content>\"\n");
    printf("  read <path>\n");
    printf("  delete <path>\n");
    printf("  mkdir <path>\n");
    printf("  rmdir <path>\n");
    printf("  cd <path>\n");
    printf("  pwd\n");
    printf("  ls\n");
    printf("  stat <path>\n");
    printf("  df\n");
    printf("  exit\n");
}

/* ---------- Disk I/O ---------- */

/*
 * Purpose: report a failed command on stderr as "Error: <cmd> <path>: <reason>"
 *          (or "Error: <cmd>: <reason>" when path is NULL).
 * Inputs:  command name, path as typed (may be NULL), reason text.
 * Side effects: flushes stdout first so output order is preserved.
 */
static void fs_error(const char *cmd, const char *path, const char *reason)
{
    fflush(stdout);
    if (path != NULL) {
        fprintf(stderr, "Error: %s %s: %s\n", cmd, path, reason);
    } else {
        fprintf(stderr, "Error: %s: %s\n", cmd, reason);
    }
}

/*
 * Purpose: guard for commands that need a valid mounted file system.
 * Inputs:  file-system state, command name for the error line.
 * Returns: 1 if mounted; otherwise prints the unmounted error and returns 0.
 */
static int require_mounted(const filesystem_t *fs, const char *cmd)
{
    if (!fs->mounted) {
        fs_error(cmd, NULL, MSG_UNMOUNTED);
        return 0;
    }
    return 1;
}

/*
 * Purpose: read one block from the disk file.
 * Inputs:  state, block number, buffer of at least BLOCK_SIZE bytes.
 * Returns: 0 on success, -1 on a bad block number or I/O failure.
 */
static int read_block(filesystem_t *fs, uint32_t block, void *buf)
{
    if (fs->disk == NULL || block >= NUM_BLOCKS ||
        fseek(fs->disk, (long)block * BLOCK_SIZE, SEEK_SET) != 0 ||
        fread(buf, 1, BLOCK_SIZE, fs->disk) != BLOCK_SIZE) {
        return -1;
    }
    return 0;
}

/*
 * Purpose: write one block to the disk file.
 * Inputs:  state, block number, buffer of BLOCK_SIZE bytes.
 * Returns: 0 on success, -1 on a bad block number or I/O failure.
 */
static int write_block(filesystem_t *fs, uint32_t block, const void *buf)
{
    if (fs->disk == NULL || block >= NUM_BLOCKS ||
        fseek(fs->disk, (long)block * BLOCK_SIZE, SEEK_SET) != 0 ||
        fwrite(buf, 1, BLOCK_SIZE, fs->disk) != BLOCK_SIZE) {
        return -1;
    }
    return 0;
}

/*
 * Purpose: write the superblock, FAT and entry table to the disk and flush.
 * Inputs:  state with a populated superblock, fat and entries.
 * Returns: 0 on success, -1 on I/O failure.
 */
static int flush_metadata(filesystem_t *fs)
{
    uint8_t block[BLOCK_SIZE];

    memset(block, 0, sizeof(block));
    memcpy(block, &fs->superblock, sizeof(fs->superblock));
    if (write_block(fs, 0, block) != 0 ||
        fseek(fs->disk, (long)FAT_START * BLOCK_SIZE, SEEK_SET) != 0 ||
        fwrite(fs->fat, 1, sizeof(fs->fat), fs->disk) != sizeof(fs->fat) ||
        fseek(fs->disk, (long)ENTRY_START * BLOCK_SIZE, SEEK_SET) != 0 ||
        fwrite(fs->entries, 1, sizeof(fs->entries), fs->disk) != sizeof(fs->entries) ||
        fflush(fs->disk) != 0) {
        return -1;
    }
    return 0;
}

/*
 * Purpose: count free data blocks in the in-memory FAT.
 * Inputs:  state with a loaded FAT.
 * Returns: number of FAT_FREE entries in the data region.
 */
static uint32_t count_free_blocks(const filesystem_t *fs)
{
    uint32_t count = 0;
    for (uint32_t b = DATA_START; b < NUM_BLOCKS; ++b) {
        if (fs->fat[b] == FAT_FREE) {
            ++count;
        }
    }
    return count;
}

/*
 * Purpose: load and validate the metadata of the open disk file.
 * Inputs:  state with fs->disk open.
 * Returns: 0 and mounts (mounted, free_blocks, cwd "/") if the file is exactly
 *          DISK_SIZE bytes and every superblock field matches its constant;
 *          otherwise -1 and the file system stays unmounted.
 */
static int load_metadata(filesystem_t *fs)
{
    uint8_t block[BLOCK_SIZE];
    const superblock_t *sb = &fs->superblock;

    fs->mounted = 0;
    if (fseek(fs->disk, 0, SEEK_END) != 0 || ftell(fs->disk) != (long)DISK_SIZE ||
        read_block(fs, 0, block) != 0) {
        return -1;
    }
    memcpy(&fs->superblock, block, sizeof(fs->superblock));
    if (sb->magic != FS_MAGIC || sb->version != FS_VERSION ||
        sb->block_size != BLOCK_SIZE || sb->total_blocks != NUM_BLOCKS ||
        sb->fat_start != FAT_START || sb->fat_blocks != FAT_BLOCKS ||
        sb->entry_start != ENTRY_START || sb->entry_blocks != ENTRY_BLOCKS ||
        sb->data_start != DATA_START || sb->max_entries != MAX_ENTRIES ||
        sb->entry_size != ENTRY_SIZE) {
        return -1;
    }
    if (fseek(fs->disk, (long)FAT_START * BLOCK_SIZE, SEEK_SET) != 0 ||
        fread(fs->fat, 1, sizeof(fs->fat), fs->disk) != sizeof(fs->fat) ||
        fseek(fs->disk, (long)ENTRY_START * BLOCK_SIZE, SEEK_SET) != 0 ||
        fread(fs->entries, 1, sizeof(fs->entries), fs->disk) != sizeof(fs->entries)) {
        return -1;
    }
    fs->free_blocks = count_free_blocks(fs);
    strcpy(fs->cwd, "/");
    fs->mounted = 1;
    return 0;
}

/* ---------- Disk setup ---------- */

/*
 * Purpose: open virtual_disk.bin at startup and mount it if it is valid.
 * Inputs:  zeroed state.
 * Returns: 0 normally (missing or invalid disk leaves the file system
 *          unmounted with a startup message); -1 on an unexpected fopen error.
 */
static int open_virtual_disk(filesystem_t *fs)
{
    fs->disk = fopen(DISK_FILE, "r+b");

    if (fs->disk == NULL) {
        /* It is valid for the disk not to exist before 'format'. */
        if (errno != ENOENT) {
            perror(DISK_FILE);
            return -1;
        }
        printf("%s\n", MSG_NO_FS);
        return 0;
    }

    if (load_metadata(fs) != 0) {
        fflush(stdout);
        fprintf(stderr, "%s\n", MSG_BAD_IMAGE);
        fclose(fs->disk);
        fs->disk = NULL;
    }
    return 0;
}

static void close_virtual_disk(filesystem_t *fs)
{
    if (fs->disk != NULL) {
        fclose(fs->disk);
        fs->disk = NULL;
    }
}

/* ---------- Operations students must implement ---------- */

/*
 * Purpose: create a fresh 1 MiB image with an empty root and mount it.
 * Inputs:  state (any previously open disk is closed and truncated).
 * Returns: 0 on success (prints the format line); -1 on I/O failure, leaving
 *          the file system unmounted after printing a disk I/O error.
 */
static int fs_format(filesystem_t *fs)
{
    uint8_t zero[BLOCK_SIZE];

    fs->mounted = 0;
    close_virtual_disk(fs);
    fs->disk = fopen(DISK_FILE, "w+b");
    if (fs->disk == NULL) {
        fs_error("format", NULL, MSG_IO);
        return -1;
    }

    memset(zero, 0, sizeof(zero));
    for (uint32_t b = 0; b < NUM_BLOCKS; ++b) {
        if (write_block(fs, b, zero) != 0) {
            close_virtual_disk(fs);
            fs_error("format", NULL, MSG_IO);
            return -1;
        }
    }

    memset(&fs->superblock, 0, sizeof(fs->superblock));
    fs->superblock.magic = FS_MAGIC;
    fs->superblock.version = FS_VERSION;
    fs->superblock.block_size = BLOCK_SIZE;
    fs->superblock.total_blocks = NUM_BLOCKS;
    fs->superblock.fat_start = FAT_START;
    fs->superblock.fat_blocks = FAT_BLOCKS;
    fs->superblock.entry_start = ENTRY_START;
    fs->superblock.entry_blocks = ENTRY_BLOCKS;
    fs->superblock.data_start = DATA_START;
    fs->superblock.max_entries = MAX_ENTRIES;
    fs->superblock.entry_size = ENTRY_SIZE;

    for (uint32_t b = 0; b < NUM_BLOCKS; ++b) {
        fs->fat[b] = (b < DATA_START) ? FAT_RESERVED : FAT_FREE;
    }

    memset(fs->entries, 0, sizeof(fs->entries));
    strcpy(fs->entries[ROOT_ID].name, "/");
    fs->entries[ROOT_ID].parent_id = ROOT_ID;
    fs->entries[ROOT_ID].first_block = FAT_END;
    fs->entries[ROOT_ID].type = TYPE_DIR;

    if (flush_metadata(fs) != 0) {
        close_virtual_disk(fs);
        fs_error("format", NULL, MSG_IO);
        return -1;
    }

    fs->free_blocks = count_free_blocks(fs);
    strcpy(fs->cwd, "/");
    fs->mounted = 1;
    printf("%s\n", MSG_FORMAT_OK);
    return 0;
}

static int fs_create(filesystem_t *fs, const char *path)
{
    if (!require_mounted(fs, "create")) return -1;
    (void)fs;
    (void)path;
    /* TODO: resolve the parent directory and create an empty regular file.
       Check entry limits and names; reject duplicates in the same directory
       without changing the disk. A new empty file needs no data blocks. */
    fprintf(stderr, "TODO: create is not implemented.\n");
    return -1;
}

static int fs_write(filesystem_t *fs, const char *path, const char *content)
{
    if (!require_mounted(fs, "write")) return -1;
    (void)fs;
    (void)path;
    (void)content;
    /* TODO: replace contents, allocating or releasing blocks as needed.
       content excludes the outer command quotes. On insufficient space,
       fail without partial changes to data, metadata, or the free map. */
    fprintf(stderr, "TODO: write is not implemented.\n");
    return -1;
}

static int fs_append(filesystem_t *fs, const char *path, const char *content)
{
    if (!require_mounted(fs, "append")) return -1;
    (void)fs;
    (void)path;
    (void)content;
    /* TODO: append content, reusing space in the current final block if possible. */
    fprintf(stderr, "TODO: append is not implemented.\n");
    return -1;
}

static int fs_read(filesystem_t *fs, const char *path)
{
    if (!require_mounted(fs, "read")) return -1;
    (void)fs;
    (void)path;
    /* TODO: reconstruct and display a file from its allocated data blocks. */
    fprintf(stderr, "TODO: read is not implemented.\n");
    return -1;
}

static int fs_delete(filesystem_t *fs, const char *path)
{
    if (!require_mounted(fs, "delete")) return -1;
    (void)fs;
    (void)path;
    /* TODO: remove a file and release all blocks owned by it. */
    fprintf(stderr, "TODO: delete is not implemented.\n");
    return -1;
}

static int fs_mkdir(filesystem_t *fs, const char *path)
{
    if (!require_mounted(fs, "mkdir")) return -1;
    (void)fs;
    (void)path;
    /* TODO: create a directory. */
    fprintf(stderr, "TODO: mkdir is not implemented.\n");
    return -1;
}

static int fs_rmdir(filesystem_t *fs, const char *path)
{
    if (!require_mounted(fs, "rmdir")) return -1;
    (void)fs;
    (void)path;
    /* TODO: resolve and remove an empty directory, but never the root. */
    fprintf(stderr, "TODO: rmdir is not implemented.\n");
    return -1;
}

static int fs_cd(filesystem_t *fs, const char *path)
{
    if (!require_mounted(fs, "cd")) return -1;
    (void)fs;
    (void)path;
    /* TODO: resolve path and update the current working directory. */
    fprintf(stderr, "TODO: cd is not implemented.\n");
    return -1;
}

/* Print the current path; keep this representation consistent in fs_cd().
 * Prints the unmounted error instead when no valid file system is mounted. */
static void fs_pwd(const filesystem_t *fs)
{
    if (!require_mounted(fs, "pwd")) return;
    printf("%s\n", fs->cwd);
}

static int fs_ls(filesystem_t *fs)
{
    if (!require_mounted(fs, "ls")) return -1;
    (void)fs;
    /* TODO: list the current directory's entries: name and type, plus
       size for regular files. */
    fprintf(stderr, "TODO: ls is not implemented.\n");
    return -1;
}

static int fs_stat(filesystem_t *fs, const char *path)
{
    if (!require_mounted(fs, "stat")) return -1;
    (void)fs;
    (void)path;
    /* TODO: show name/type, and size plus data-block count for a file;
       show direct-child count excluding '.' and '..' for a directory. */
    fprintf(stderr, "TODO: stat is not implemented.\n");
    return -1;
}

/*
 * Purpose: report block usage; reserved metadata blocks count as used.
 * Inputs:  mounted state.
 * Returns: 0 on success; -1 (with the unmounted error) otherwise.
 */
static int fs_df(filesystem_t *fs)
{
    uint32_t used, data;

    if (!require_mounted(fs, "df")) return -1;
    used = fs->superblock.total_blocks - fs->free_blocks;
    data = used - fs->superblock.data_start;
    printf("%-20s%u bytes\n", "Block size:", fs->superblock.block_size);
    printf("%-20s%u\n", "Total blocks:", fs->superblock.total_blocks);
    printf("%-20s%u (metadata %u + data %u)\n", "Used blocks:", used,
           fs->superblock.data_start, data);
    printf("%-20s%u\n", "Free blocks:", fs->free_blocks);
    printf("%-20s%u\n", "Metadata reserved:", fs->superblock.data_start);
    return 0;
}

/* ---------- Command interpreter ---------- */

static int require_arg(const char *cmd, const char *arg)
{
    if (arg == NULL || *arg == '\0') {
        fprintf(stderr, "Usage error: %s requires an argument.\n", cmd);
        return 0;
    }
    return 1;
}

/* Reject extra arguments before calling an operation. */
static int no_extra_args(const char *cmd, char *cursor)
{
    if (*skip_spaces(cursor) != '\0') {
        fprintf(stderr, "Usage error: unexpected arguments for %s.\n", cmd);
        return 0;
    }
    return 1;
}

/* Remove only the enclosing quotes; the returned content may be empty. */
static char *quoted_content(const char *cmd, char *cursor)
{
    char *content = skip_spaces(cursor);
    size_t length = strlen(content);
    while (length > 0 && isspace((unsigned char)content[length - 1])) {
        --length;
    }
    if (length < 2 || content[0] != '"' || content[length - 1] != '"') {
        fprintf(stderr, "Usage: %s <path> \"<content>\"\n", cmd);
        return NULL;
    }
    content[length - 1] = '\0';
    return content + 1;
}

static void execute_command(filesystem_t *fs, char *line, int *running)
{
    char *cursor = line;
    char *cmd = next_token(&cursor);
    char *arg;

    if (cmd == NULL) {
        return;
    }

    if (strcmp(cmd, "format") == 0) {
        if (no_extra_args(cmd, cursor)) fs_format(fs);
    } else if (strcmp(cmd, "create") == 0) {
        arg = next_token(&cursor);
        if (require_arg(cmd, arg) && no_extra_args(cmd, cursor)) fs_create(fs, arg);
    } else if (strcmp(cmd, "write") == 0) {
        arg = next_token(&cursor);
        if (require_arg(cmd, arg)) {
            char *content = quoted_content(cmd, cursor);
            if (content != NULL) fs_write(fs, arg, content);
        }
    } else if (strcmp(cmd, "append") == 0) {
        arg = next_token(&cursor);
        if (require_arg(cmd, arg)) {
            char *content = quoted_content(cmd, cursor);
            if (content != NULL) fs_append(fs, arg, content);
        }
    } else if (strcmp(cmd, "read") == 0) {
        arg = next_token(&cursor);
        if (require_arg(cmd, arg) && no_extra_args(cmd, cursor)) fs_read(fs, arg);
    } else if (strcmp(cmd, "delete") == 0) {
        arg = next_token(&cursor);
        if (require_arg(cmd, arg) && no_extra_args(cmd, cursor)) fs_delete(fs, arg);
    } else if (strcmp(cmd, "mkdir") == 0) {
        arg = next_token(&cursor);
        if (require_arg(cmd, arg) && no_extra_args(cmd, cursor)) fs_mkdir(fs, arg);
    } else if (strcmp(cmd, "rmdir") == 0) {
        arg = next_token(&cursor);
        if (require_arg(cmd, arg) && no_extra_args(cmd, cursor)) fs_rmdir(fs, arg);
    } else if (strcmp(cmd, "cd") == 0) {
        arg = next_token(&cursor);
        if (require_arg(cmd, arg) && no_extra_args(cmd, cursor)) fs_cd(fs, arg);
    } else if (strcmp(cmd, "pwd") == 0) {
        if (no_extra_args(cmd, cursor)) fs_pwd(fs);
    } else if (strcmp(cmd, "ls") == 0) {
        if (no_extra_args(cmd, cursor)) fs_ls(fs);
    } else if (strcmp(cmd, "stat") == 0) {
        arg = next_token(&cursor);
        if (require_arg(cmd, arg) && no_extra_args(cmd, cursor)) fs_stat(fs, arg);
    } else if (strcmp(cmd, "df") == 0) {
        if (no_extra_args(cmd, cursor)) fs_df(fs);
    } else if (strcmp(cmd, "exit") == 0) {
        if (no_extra_args(cmd, cursor)) *running = 0;
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
    }
}

int main(void)
{
    filesystem_t fs;
    char *line = NULL;
    size_t line_capacity = 0;
    int running = 1;

    memset(&fs, 0, sizeof(fs));
    strcpy(fs.cwd, "/");

    if (open_virtual_disk(&fs) != 0) {
        fprintf(stderr, "Failed to initialize the virtual disk.\n");
        return EXIT_FAILURE;
    }

    printf("Mini File System Simulator\n");
    print_commands();

    while (running) {
        printf("minifs> ");
        fflush(stdout);

        /* Read a whole command; no assignment-specific line limit is imposed. */
        if (getline(&line, &line_capacity, stdin) == -1) {
            if (ferror(stdin) || !feof(stdin)) {
                perror("command input");
                free(line);
                close_virtual_disk(&fs);
                return EXIT_FAILURE;
            }
            putchar('\n');
            break;
        }

        trim_newline(line);
        execute_command(&fs, line, &running);
    }

    free(line);
    close_virtual_disk(&fs);
    return EXIT_SUCCESS;
}
