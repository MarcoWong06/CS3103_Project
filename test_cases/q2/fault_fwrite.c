/*
 * Test helper for run_tests.sh (Linux only): an LD_PRELOAD library that makes
 * one write to the virtual disk fail, so the minifs I/O rollback path can be
 * tested. Nothing in problem2.c depends on it.
 *
 * MINIFS_FAIL_AT=<byte offset>: the first fwrite() to a file stream other
 * than stdout/stderr that starts at that stream position writes nothing and
 * returns 0 with errno = EIO. The fault fires once per process, so a later
 * write at the same offset (for example the rollback's metadata flush) works.
 *
 * Build: gcc -shared -fPIC -o fault_fwrite.so fault_fwrite.c -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

typedef size_t (*fwrite_fn)(const void *, size_t, size_t, FILE *);

/*
 * Purpose: fwrite() replacement that fails once at MINIFS_FAIL_AT.
 * Inputs:  as fwrite().
 * Returns: 0 (errno = EIO) for the faulted call; otherwise the real fwrite().
 */
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    static fwrite_fn real_fwrite;
    static int fired;
    const char *at = getenv("MINIFS_FAIL_AT");

    if (real_fwrite == NULL) {
        *(void **)&real_fwrite = dlsym(RTLD_NEXT, "fwrite");
    }
    if (!fired && at != NULL && stream != stdout && stream != stderr &&
        ftell(stream) == strtol(at, NULL, 10)) {
        fired = 1;
        errno = EIO;
        return 0;
    }
    return real_fwrite(ptr, size, nmemb, stream);
}
