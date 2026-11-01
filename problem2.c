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

#define TYPE_FILE       1
#define TYPE_DIR        2

/*
 * A minimal metadata definition is provided as a starting point.
 * You may modify or extend it to support your design.
 *
 * Example metadata table for /courses/os/report.txt:
 *   entry ID    name          parent_id
 *       0       /                 0       (root is its own parent)
 *       1       courses           0
 *       2       os                1
 *       3       report.txt        2
 * Each ID is an index in your metadata table. To resolve this path,
 * start at root and find each name among the current directory's children.
 * Persist name and parent_id in virtual_disk.bin to retain the hierarchy.
 * This is one possible representation; an equivalent design is allowed.
 */
typedef struct {
    char name[MAX_NAME_LEN + 1]; /* One name, not the full path. */
    int parent_id;              /* Parent directory's metadata-table index. */
    uint32_t size;
    uint8_t type;

    /* TODO: add data-block locations and any other fields your design needs.
       Initialize parent_id when creating each file or directory, and save
       the metadata table to virtual_disk.bin as part of your disk layout.
       No particular allocation scheme or one-block metadata layout is required. */
} file_entry_t;

/*
 * Example superblock. Students may modify or extend this structure.
 */
typedef struct {
    uint32_t magic;
    uint32_t block_size;
    uint32_t total_blocks;

    /* TODO: add fields describing your on-disk layout. */
} superblock_t;

/* File-system state used while the simulator is running. */
typedef struct {
    FILE *disk;
    superblock_t superblock;
    char cwd[MAX_PATH_LEN]; /* May be replaced by a current-directory ID. */

    /* TODO: add metadata and free-space state for your design.
       Share one path-resolution mechanism across file/directory commands.
       Resolve absolute paths from root, relative paths from the current
       directory, and support '.' and '..'. Save persistent state on disk. */
} filesystem_t;

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

/* ---------- Disk setup ---------- */

static int open_virtual_disk(filesystem_t *fs)
{
    fs->disk = fopen(DISK_FILE, "r+b");

    if (fs->disk == NULL) {
        /* It is valid for the disk not to exist before 'format'. */
        if (errno != ENOENT) {
            perror(DISK_FILE);
            return -1;
        }
        return 0;
    }

    /* TODO: load persistent metadata, allocation state, and directory
       relationships from virtual_disk.bin. Do not format on restart.
       Track whether the loaded file system is usable in your chosen design. */
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

static int fs_format(filesystem_t *fs)
{
    (void)fs;
    /* TODO: initialize a DISK_SIZE-byte disk, your metadata/free-space
       layout, and an empty root directory. Root counts toward MAX_ENTRIES.
       Reset the current directory to root. */
    fprintf(stderr, "TODO: format is not implemented.\n");
    return -1;
}

static int fs_create(filesystem_t *fs, const char *path)
{
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
    (void)fs;
    (void)path;
    (void)content;
    /* TODO: append content, reusing space in the current final block if possible. */
    fprintf(stderr, "TODO: append is not implemented.\n");
    return -1;
}

static int fs_read(filesystem_t *fs, const char *path)
{
    (void)fs;
    (void)path;
    /* TODO: reconstruct and display a file from its allocated data blocks. */
    fprintf(stderr, "TODO: read is not implemented.\n");
    return -1;
}

static int fs_delete(filesystem_t *fs, const char *path)
{
    (void)fs;
    (void)path;
    /* TODO: remove a file and release all blocks owned by it. */
    fprintf(stderr, "TODO: delete is not implemented.\n");
    return -1;
}

static int fs_mkdir(filesystem_t *fs, const char *path)
{
    (void)fs;
    (void)path;
    /* TODO: create a directory. */
    fprintf(stderr, "TODO: mkdir is not implemented.\n");
    return -1;
}

static int fs_rmdir(filesystem_t *fs, const char *path)
{
    (void)fs;
    (void)path;
    /* TODO: resolve and remove an empty directory, but never the root. */
    fprintf(stderr, "TODO: rmdir is not implemented.\n");
    return -1;
}

static int fs_cd(filesystem_t *fs, const char *path)
{
    (void)fs;
    (void)path;
    /* TODO: resolve path and update the current working directory. */
    fprintf(stderr, "TODO: cd is not implemented.\n");
    return -1;
}

/* Print the current path; keep this representation consistent in fs_cd(). */
static void fs_pwd(const filesystem_t *fs)
{
    printf("%s\n", fs->cwd);
}

static int fs_ls(filesystem_t *fs)
{
    (void)fs;
    /* TODO: list the current directory's entries: name and type, plus
       size for regular files. */
    fprintf(stderr, "TODO: ls is not implemented.\n");
    return -1;
}

static int fs_stat(filesystem_t *fs, const char *path)
{
    (void)fs;
    (void)path;
    /* TODO: show name/type, and size plus data-block count for a file;
       show direct-child count excluding '.' and '..' for a directory. */
    fprintf(stderr, "TODO: stat is not implemented.\n");
    return -1;
}

static int fs_df(filesystem_t *fs)
{
    (void)fs;
    /* TODO: report total, used, free, metadata-reserved blocks, and block
       size. Reserved blocks count as used; used + free = NUM_BLOCKS. */
    fprintf(stderr, "TODO: df is not implemented.\n");
    return -1;
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
    printf("Student starter: file-system operations are TODOs.\n");
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
