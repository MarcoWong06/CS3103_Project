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
#define TOTAL_ITEMS \
    (NUM_PRODUCERS * ITEMS_PER_PRODUCER)

#define TERMINATION_ID (-1)

typedef struct {
    int id;
    int producer_id;
    int value;
} item_t; // producer A

typedef struct {
    int id;
    int producer_id;
    int worker_id;
    long result;
} result_t; // after done A

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
    /* Shared result records used by the consumers and checked by the parent. */
    result_t records[NUM_PRODUCERS * ITEMS_PER_PRODUCER];
    int seen[NUM_PRODUCERS * ITEMS_PER_PRODUCER];
    int collected_count;
    int out_of_range_count;
    sem_t records_mutex;
} shared_t;

/* Generate this producer's input items and insert them into Buffer A. */
static void producer(shared_t *sh, int producer_id)
{
    /* Generate each item outside the buffer's critical section. */
    for (int j = 0; j < ITEMS_PER_PRODUCER; ++j) {
        item_t item;
        item.id = producer_id * ITEMS_PER_PRODUCER + j;
        item.producer_id = producer_id;
        item.value = item.id + 1;

        sem_wait(&sh->input.empty);
        sem_wait(&sh->input.mutex);

        sh->input.data[sh->input.in] = item;
        sh->input.in = (sh->input.in + 1) % BUFFER_A_SIZE;

        sem_post(&sh->input.mutex);
        sem_post(&sh->input.full);
    }

    /* The parent adds termination items after all producers have finished. */
}

/* Transfer input items through the computation stage into Buffer B. */
static void worker(shared_t *sh, int worker_id)
{
    for (;;) {
        item_t item;
        result_t result;

        /* Remove one item from Buffer A. */
        sem_wait(&sh->input.full);
        sem_wait(&sh->input.mutex);

        item = sh->input.data[sh->input.out];
        sh->input.out = (sh->input.out + 1) % BUFFER_A_SIZE;

        sem_post(&sh->input.mutex);
        sem_post(&sh->input.empty);

        /* A termination item is not processed or copied into Buffer B. */
        if (item.id == TERMINATION_ID)
            break;

        /* Compute outside the Buffer A critical section. */
        result.id = item.id;
        result.producer_id = item.producer_id;
        result.worker_id = worker_id;
        result.result = (long)item.value * item.value;

        /* Insert the completed result into Buffer B. */
        sem_wait(&sh->output.empty);
        sem_wait(&sh->output.mutex);

        sh->output.data[sh->output.in] = result;
        sh->output.in = (sh->output.in + 1) % BUFFER_B_SIZE;

        sem_post(&sh->output.mutex);
        sem_post(&sh->output.full);
    }
}

/* Collect Buffer B results into synchronized shared records. */
static void consumer(shared_t *sh, int consumer_id)
{
    (void)consumer_id;

    for (;;) {
        result_t result;

        /* Remove one result from Buffer B. */
        sem_wait(&sh->output.full);
        sem_wait(&sh->output.mutex);

        result = sh->output.data[sh->output.out];
        sh->output.out = (sh->output.out + 1) % BUFFER_B_SIZE;

        sem_post(&sh->output.mutex);
        sem_post(&sh->output.empty);

        /* Do not store a termination record. */
        if (result.id == TERMINATION_ID)
            break;

        /* Protect every update to the shared verification records. */
        sem_wait(&sh->records_mutex);

        if (result.id < 0 || result.id >= TOTAL_ITEMS) {
            sh->out_of_range_count++;
        } else {
            /* Keep the first record; seen[] counts all occurrences. */
            if (sh->seen[result.id] == 0)
                sh->records[result.id] = result;

            sh->seen[result.id]++;
            sh->collected_count++;
        }

        sem_post(&sh->records_mutex);
    }
}

/* Initialize the shared buffer indices and process-shared semaphores. */
static int init_shared(shared_t *sh)
{
    sh->input.in = sh->input.out = 0;
    sh->output.in = sh->output.out = 0;
    sh->collected_count = 0;
    sh->out_of_range_count = 0;

    for (int i = 0; i < TOTAL_ITEMS; ++i) {
        sh->seen[i] = 0;
        sh->records[i].id = -1;
        sh->records[i].producer_id = -1;
        sh->records[i].worker_id = -1;
        sh->records[i].result = 0;
    }

    if (sem_init(&sh->input.mutex, 1, 1) == -1 ||
        sem_init(&sh->input.empty, 1, BUFFER_A_SIZE) == -1 ||
        sem_init(&sh->input.full, 1, 0) == -1 ||
        sem_init(&sh->output.mutex, 1, 1) == -1 ||
        sem_init(&sh->output.empty, 1, BUFFER_B_SIZE) == -1 ||
        sem_init(&sh->output.full, 1, 0) == -1 ||
        sem_init(&sh->records_mutex, 1, 1) == -1) {
        perror("sem_init");
        return -1;
    }

    return 0;
}

/* Insert a termination item into Buffer A using the normal buffer protocol. */
static void send_worker_termination(shared_t *sh)
{
    item_t item;
    item.id = TERMINATION_ID;
    item.producer_id = -1;
    item.value = 0;

    sem_wait(&sh->input.empty);
    sem_wait(&sh->input.mutex);

    sh->input.data[sh->input.in] = item;
    sh->input.in = (sh->input.in + 1) % BUFFER_A_SIZE;

    sem_post(&sh->input.mutex);
    sem_post(&sh->input.full);
}

/* Insert a termination record into Buffer B using the normal buffer protocol. */
static void send_consumer_termination(shared_t *sh)
{
    result_t result;
    result.id = TERMINATION_ID;
    result.producer_id = -1;
    result.worker_id = -1;
    result.result = 0;

    sem_wait(&sh->output.empty);
    sem_wait(&sh->output.mutex);

    sh->output.data[sh->output.in] = result;
    sh->output.in = (sh->output.in + 1) % BUFFER_B_SIZE;

    sem_post(&sh->output.mutex);
    sem_post(&sh->output.full);
}

/* Wait for one child and verify that it exited successfully. */
static int wait_for_child(pid_t pid)
{
    int status;

    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        return 0;
    }

    return WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS;
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

    pid_t producer_pids[NUM_PRODUCERS];
    pid_t worker_pids[NUM_WORKERS];
    pid_t consumer_pids[NUM_CONSUMERS];
    int child_status_ok = 1;

    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(EXIT_FAILURE); }
        if (pid == 0) { producer(sh, i); _exit(EXIT_SUCCESS); }
        producer_pids[i] = pid;
    }

    for (int i = 0; i < NUM_WORKERS; ++i) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(EXIT_FAILURE); }
        if (pid == 0) { worker(sh, i); _exit(EXIT_SUCCESS); }
        worker_pids[i] = pid;
    }

    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(EXIT_FAILURE); }
        if (pid == 0) { consumer(sh, i); _exit(EXIT_SUCCESS); }
        consumer_pids[i] = pid;
    }

    /* Wait only for producers first. Workers and consumers remain active, so
       bounded buffers can continue draining and the pipeline cannot deadlock. */
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        if (!wait_for_child(producer_pids[i]))
            child_status_ok = 0;
    }

    /* Every ordinary input item has now been inserted. Send exactly one
       termination item to each worker. */
    for (int i = 0; i < NUM_WORKERS; ++i)
        send_worker_termination(sh);

    for (int i = 0; i < NUM_WORKERS; ++i) {
        if (!wait_for_child(worker_pids[i]))
            child_status_ok = 0;
    }

    /* Every ordinary result has now been inserted. Send exactly one
       termination record to each consumer. */
    for (int i = 0; i < NUM_CONSUMERS; ++i)
        send_consumer_termination(sh);

    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        if (!wait_for_child(consumer_pids[i]))
            child_status_ok = 0;
    }

    int verification_ok = child_status_ok;
    int total_items = NUM_PRODUCERS * ITEMS_PER_PRODUCER;

    if (!child_status_ok) {
        fprintf(stderr, "Verification failed: a child process did not exit successfully.\n");
    }

    if (sh->out_of_range_count != 0) {
        fprintf(stderr,
                "Verification failed: %d out-of-range result(s).\n",
                sh->out_of_range_count);
        verification_ok = 0;
    }

    if (sh->collected_count != total_items) {
        fprintf(stderr,
                "Verification failed: collected %d, expected %d.\n",
                sh->collected_count, total_items);
        verification_ok = 0;
    }

    /* records[id] is indexed by ID, so results are printed in ascending order. */
    for (int id = 0; id < total_items; ++id) {
        int expected_producer = id / ITEMS_PER_PRODUCER;
        long expected_result = (long)(id + 1) * (id + 1);

        if (sh->seen[id] != 1) {
            fprintf(stderr,
                    "Verification failed: ID %d occurred %d time(s).\n",
                    id, sh->seen[id]);
            verification_ok = 0;
            continue;
        }

        if (sh->records[id].id != id) {
            fprintf(stderr,
                    "Verification failed: stored ID for position %d is incorrect.\n",
                    id);
            verification_ok = 0;
            continue;
        }

        if (sh->records[id].producer_id != expected_producer) {
            fprintf(stderr,
                    "Verification failed: producer ID for item %d is incorrect.\n",
                    id);
            verification_ok = 0;
            continue;
        }

        if (sh->records[id].worker_id < 0 ||
            sh->records[id].worker_id >= NUM_WORKERS) {
            fprintf(stderr,
                    "Verification failed: worker ID for item %d is out of range.\n",
                    id);
            verification_ok = 0;
            continue;
        }

        if (sh->records[id].result != expected_result) {
            fprintf(stderr,
                    "Verification failed: result for item %d is incorrect.\n",
                    id);
            verification_ok = 0;
            continue;
        }

        printf("id=%d producer=%d worker=%d result=%ld\n",
               sh->records[id].id,
               sh->records[id].producer_id,
               sh->records[id].worker_id,
               sh->records[id].result);
    }

    if (verification_ok) {
        printf("Verification successful: all %d results are correct.\n",
               total_items);
    }

    sem_destroy(&sh->records_mutex);
    sem_destroy(&sh->input.mutex);
    sem_destroy(&sh->input.empty);
    sem_destroy(&sh->input.full);
    sem_destroy(&sh->output.mutex);
    sem_destroy(&sh->output.empty);
    sem_destroy(&sh->output.full);
    munmap(sh, sizeof(*sh));

    if (!verification_ok) {
        fprintf(stderr, "Verification failed.\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
