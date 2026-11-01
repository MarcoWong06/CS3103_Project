/* Student starter for Problem 1. Complete the TODOs before submission. */
/* Build on Linux: gcc -std=gnu11 -Wall -Wextra -pthread problem1.c -o problem1 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>

/* Edit these macros and recompile to change the configuration. */
#define BUFFER_A_SIZE 5
#define BUFFER_B_SIZE 5
#define NUM_PRODUCERS 3
#define NUM_WORKERS 3
#define NUM_CONSUMERS 2
#define ITEMS_PER_PRODUCER 4

typedef struct {
    int id;
    int producer_id;
    int value;
} item_t;

typedef struct {
    int id;
    int producer_id;
    int worker_id;
    long result;
} result_t;

typedef struct {
    item_t data[BUFFER_A_SIZE];
    int in;
    int out;
    sem_t mutex;
    sem_t empty;
    sem_t full;
} input_buffer_t;

typedef struct {
    result_t data[BUFFER_B_SIZE];
    int in;
    int out;
    sem_t mutex;
    sem_t empty;
    sem_t full;
} output_buffer_t;

typedef struct {
    input_buffer_t input;
    output_buffer_t output;
    /* TODO: add shared result records and termination state as needed.
       You may extend these structures and add synchronization. */
} shared_t;

/* Generate this producer's input items and insert them into Buffer A. */
static void producer(shared_t *sh, int producer_id)
{
    (void)sh;
    /* TODO: for j = 0 .. ITEMS_PER_PRODUCER-1, generate an item with
       id = producer_id * ITEMS_PER_PRODUCER + j, producer_id as supplied,
       and value = id + 1. Insert it into sh->input with synchronization.
       Generate the item outside the buffer's critical section.
       Use BUFFER_A_SIZE for circular-buffer wraparound.
       Complete this producer's part of your termination protocol. */
    fprintf(stderr, "Producer %d: TODO\n", producer_id);
}

/* Transfer input items through the computation stage into Buffer B. */
static void worker(shared_t *sh, int worker_id)
{
    (void)sh;
    /* TODO: remove an item from sh->input and release its lock.
       Compute (long)item.value * item.value outside the critical section.
       Preserve id and producer_id, and record this worker_id.
       Insert the result into sh->output using BUFFER_B_SIZE for wraparound.
       Follow your termination protocol; an empty buffer is not completion. */
    fprintf(stderr, "Worker %d: TODO\n", worker_id);
}

/* Collect Buffer B results into synchronized shared records. */
static void consumer(shared_t *sh, int consumer_id)
{
    (void)sh;
    /* TODO: remove results from sh->output and save them in shared memory.
       Synchronize shared updates and retain enough information for the
       parent to detect missing, duplicate, or out-of-range item IDs.
       Exclude termination records and follow your termination protocol.
       The parent prints the final results in ascending item ID order. */
    fprintf(stderr, "Consumer %d: TODO\n", consumer_id);
}

/* Initialize the shared buffer indices and process-shared semaphores. */
static int init_shared(shared_t *sh)
{
    sh->input.in = sh->input.out = 0;
    sh->output.in = sh->output.out = 0;

    if (sem_init(&sh->input.mutex, 1, 1) == -1 ||
        sem_init(&sh->input.empty, 1, BUFFER_A_SIZE) == -1 ||
        sem_init(&sh->input.full, 1, 0) == -1 ||
        sem_init(&sh->output.mutex, 1, 1) == -1 ||
        sem_init(&sh->output.empty, 1, BUFFER_B_SIZE) == -1 ||
        sem_init(&sh->output.full, 1, 0) == -1) {
        perror("sem_init");
        return -1;
    }
    /* TODO: initialize any added shared records, synchronization, and
       termination state here, before any child process is created. */
    return 0;
}

/* Create shared resources, launch all roles, verify results, and clean up. */
int main(void)
{
    shared_t *sh = mmap(NULL, sizeof(*sh), PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sh == MAP_FAILED) {
        perror("mmap");
        return EXIT_FAILURE;
    }

    if (init_shared(sh) != 0) {
        munmap(sh, sizeof(*sh));
        return EXIT_FAILURE;
    }

    int created = 0;

    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(EXIT_FAILURE); }
        if (pid == 0) { producer(sh, i); _exit(EXIT_SUCCESS); }
        ++created;
    }

    for (int i = 0; i < NUM_WORKERS; ++i) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(EXIT_FAILURE); }
        if (pid == 0) { worker(sh, i); _exit(EXIT_SUCCESS); }
        ++created;
    }

    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(EXIT_FAILURE); }
        if (pid == 0) { consumer(sh, i); _exit(EXIT_SUCCESS); }
        ++created;
    }

    /* All process groups must be started before waiting for children.
       TODO: add any parent-side actions required by your termination
       protocol at the appropriate point; do not wait for a process that
       still needs a termination action from the parent. */
    /* TODO: collect and check each child's exit status, rather than
       discarding it with wait(NULL). */
    for (int i = 0; i < created; ++i)
        wait(NULL);

    int verification_ok = 0;
    /* TODO: before unmapping shared memory, verify the collected results:
       - every ID from 0 to NUM_PRODUCERS * ITEMS_PER_PRODUCER - 1 occurs once;
       - producer_id and result match the generated item;
       - worker_id is within 0 .. NUM_WORKERS-1;
       - the total is correct, with no out-of-range IDs.
       Print the results in ascending item ID order.
       Set verification_ok only after verification and child-status checks
       have succeeded. This placeholder is not a completed verifier. */

    /* TODO: destroy any additional semaphores you initialize. */
    sem_destroy(&sh->input.mutex);
    sem_destroy(&sh->input.empty);
    sem_destroy(&sh->input.full);
    sem_destroy(&sh->output.mutex);
    sem_destroy(&sh->output.empty);
    sem_destroy(&sh->output.full);
    munmap(sh, sizeof(*sh));

    if (!verification_ok) {
        fprintf(stderr, "Starter incomplete or verification failed.\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
