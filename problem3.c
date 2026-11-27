/*
 * CS3103 Operating Systems Project - Problem 3: Scheduling
 *
 * Simulates the five required scheduling configurations on one input file -
 * FCFS, non-preemptive SJF, preemptive Priority, Round Robin (q = 3) and
 * Round Robin (q = 6) - in that order, each on a fresh copy of the jobs.
 * The optional three-level MLFQ of the advanced part is printed as a sixth
 * block only when --mlfq is given, so the default output keeps exactly the
 * required five-configuration format.
 *
 * Build:
 *   gcc -std=c11 -Wall -Wextra problem3.c -o problem3
 *   gcc -std=gnu11 -Wall -Wextra problem3.c -o problem3
 * Usage:
 *   ./problem3 <orders_file>
 *   ./problem3 --mlfq <orders_file>
 *
 * Design notes (also documented in the report):
 *   - Times are kept in long long, so large arrival and burst values cannot
 *     overflow accumulated completion times.
 *   - The scheduling rules never depend on the file order beyond the
 *     documented tie-breaking (arrival time, then input position).
 *   - Consecutive execution of the same job is merged into one Gantt segment,
 *     except in Round Robin, where each quantum stays a separate segment so
 *     the quantum remains visible.
 *   - Zero context-switch cost is assumed, as required by the specification.
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN_PRIORITY 1          /* most urgent priority value */
#define MAX_PRIORITY 5          /* least urgent priority value */
#define NO_JOB (-1)             /* no selected job or an idle Gantt segment */
#define IDLE_LABEL "IDLE"       /* label used only for an idle Gantt segment */
#define INITIAL_CAPACITY 32     /* first allocation of the job and segment arrays */

static const char input_separators[] = " \t\n\r\v\f";

/* One input job, stored in the order in which it appeared in the file. */
typedef struct {
    char *job_id;
    int arrival_time;
    int burst_time;
    int priority;
} Job;

/* All jobs of one input file, in input-file order. */
typedef struct {
    Job *jobs;
    int count;
    size_t capacity;
} JobQueue;

/* One contiguous interval in which the CPU runs the same job. */
typedef struct {
    int job_index;              /* NO_JOB for an idle interval */
    long long start;
    long long end;
} GanttSegment;

/*
 * A produced schedule.  It also keeps a borrowed view of the job table so
 * print_schedule(), whose parameters are fixed by the specification, can
 * print job ids, arrival times and burst times.
 */
typedef struct {
    GanttSegment *segs;
    size_t count;
    size_t capacity;
    long long finish;           /* end of the last segment */
    const Job *jobs;            /* borrowed from the job queue */
    int job_count;
    int failed;                 /* nonzero after a scheduling allocation failure */
} GanttChart;

/* One row of completion, turnaround, and waiting times. */
typedef struct {
    long long completion;
    long long turnaround;
    long long waiting;
} JobMetrics;

/* Results in input order and averages of one scheduling configuration. */
typedef struct {
    JobMetrics *rows;
    double avg_tat;
    double avg_wt;
    double cpu_util;
} Metrics;

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

/*
 * Grow array, whose elements each take element_size bytes. Update *capacity
 * only on success. Return the new pointer or NULL, leaving array valid.
 */
static void *grow_array(void *array, size_t *capacity, size_t element_size)
{
    size_t limit = SIZE_MAX / element_size;
    size_t next = *capacity ? *capacity : INITIAL_CAPACITY;

    if (*capacity >= limit)
        return NULL;
    if (*capacity > 0)
        next = *capacity <= limit / 2 ? *capacity * 2 : limit;
    if (next > limit)
        next = limit;

    void *grown = realloc(array, next * element_size);
    if (grown != NULL)
        *capacity = next;
    return grown;
}

/* Return 1 when the token [text, text + len) equals word, ignoring case. */
static int token_matches_ignore_case(const char *text, size_t len, const char *word)
{
    if (strlen(word) != len)
        return 0;
    for (size_t i = 0; i < len; ++i) {
        if (tolower((unsigned char)text[i]) != tolower((unsigned char)word[i]))
            return 0;
    }
    return 1;
}

/* Return 1 if row contains the four documented header labels in order. */
static int is_job_header(const char *row)
{
    static const char *const labels[] = {"job_id", "arrival", "burst", "priority"};

    for (size_t i = 0; i < sizeof labels / sizeof *labels; ++i) {
        size_t length = strcspn(row, input_separators);
        if (!token_matches_ignore_case(row, length, labels[i]))
            return 0;
        row += length;
        row += strspn(row, input_separators);
    }
    return *row == '\0';
}

/*
 * Parse one whitespace-separated base-10 integer from *cursor into *value.
 * Advance *cursor on success. Return 0 on success or -1 for an invalid field.
 */
static int parse_long_field(const char **cursor, long *value)
{
    const char *p = *cursor;
    char *end = NULL;
    long parsed;

    if (!isspace((unsigned char)*p))
        return -1;
    errno = 0;
    parsed = strtol(p, &end, 10);
    if (end == p || errno == ERANGE)
        return -1;
    *value = parsed;
    *cursor = end;
    return 0;
}

/*
 * Read the arrival, burst and priority fields after the ID into job.
 * Return 0 on success or -1 for a malformed or out-of-range field.
 */
static int parse_job_fields(const char *fields, Job *job)
{
    long arrival;
    long burst;
    long priority;

    if (parse_long_field(&fields, &arrival) != 0 ||
        parse_long_field(&fields, &burst) != 0 ||
        parse_long_field(&fields, &priority) != 0 ||
        fields[strspn(fields, input_separators)] != '\0' ||
        arrival < 0 || arrival > INT_MAX ||
        burst <= 0 || burst > INT_MAX ||
        priority < MIN_PRIORITY || priority > MAX_PRIORITY)
        return -1;

    job->arrival_time = (int)arrival;
    job->burst_time = (int)burst;
    job->priority = (int)priority;
    return 0;
}

/* Free queue's job IDs and job array, then reset the queue to empty. */
static void queue_destroy(JobQueue *queue)
{
    for (int i = 0; i < queue->count; ++i)
        free(queue->jobs[i].job_id);
    free(queue->jobs);
    *queue = (JobQueue){0};
}

/*
 * Initialize queue from filepath, preserving input order.
 * Return the number of jobs read, or -1 on any error.  Errors are reported on
 * stderr, and a failed call leaves an empty queue behind.
 */
int load_jobs(const char *filepath, JobQueue *queue)
{
    FILE *fp;
    char *line = NULL;
    size_t line_capacity = 0;
    size_t line_number = 0;
    int seen_content = 0;
    int result = -1;
    ssize_t line_length;

    *queue = (JobQueue){0};

    if (filepath == NULL) {
        fprintf(stderr, "error: no input file given\n");
        return -1;
    }
    fp = fopen(filepath, "r");
    if (fp == NULL) {
        fprintf(stderr, "error: cannot open '%s': %s\n", filepath, strerror(errno));
        return -1;
    }

    while ((line_length = getline(&line, &line_capacity, fp)) >= 0) {
        const char *row = line + strspn(line, input_separators);

        ++line_number;
        if (memchr(line, '\0', (size_t)line_length) != NULL) {
            fprintf(stderr, "error: %s: line %zu contains a NUL byte\n", filepath, line_number);
            goto done;
        }
        if (*row == '\0')
            continue;

        if (!seen_content) {
            seen_content = 1;
            if (is_job_header(row))
                continue;
        }

        size_t id_length = strcspn(row, input_separators);
        Job job;
        if (parse_job_fields(row + id_length, &job) != 0) {
            fprintf(stderr,
                    "error: %s: line %zu: expected 'job_id arrival burst priority' "
                    "with arrival >= 0, burst > 0 and priority %d..%d\n",
                    filepath, line_number, MIN_PRIORITY, MAX_PRIORITY);
            goto done;
        }

        if (queue->count == INT_MAX) {
            fprintf(stderr, "error: '%s' exceeds the representable job count\n", filepath);
            goto done;
        }
        if ((size_t)queue->count == queue->capacity) {
            Job *grown = grow_array(queue->jobs, &queue->capacity, sizeof *grown);
            if (grown == NULL) {
                fprintf(stderr, "error: out of memory while reading '%s'\n", filepath);
                goto done;
            }
            queue->jobs = grown;
        }
        job.job_id = strndup(row, id_length);
        if (job.job_id == NULL) {
            fprintf(stderr, "error: out of memory while reading '%s'\n", filepath);
            goto done;
        }
        queue->jobs[queue->count++] = job;
    }

    if (ferror(fp) || !feof(fp))
        fprintf(stderr, "error: cannot read '%s': %s\n", filepath, strerror(errno));
    else
        result = queue->count;

done:
    if (fclose(fp) == EOF && result >= 0) {
        fprintf(stderr, "error: cannot close '%s': %s\n", filepath, strerror(errno));
        result = -1;
    }
    free(line);
    if (result < 0)
        queue_destroy(queue);
    return result;
}

/* ------------------------------------------------------------------ */
/* Gantt chart                                                        */
/* ------------------------------------------------------------------ */

/* Initialize chart with no segments and borrow queue's job table. */
static void chart_init(GanttChart *chart, const JobQueue *queue)
{
    *chart = (GanttChart){.jobs = queue->jobs, .job_count = queue->count};
}

/* Free chart's segment array and reset the chart to empty. */
static void chart_release(GanttChart *chart)
{
    free(chart->segs);
    *chart = (GanttChart){0};
}

/* Report a scheduling allocation failure and mark chart incomplete. */
static void chart_out_of_memory(GanttChart *chart)
{
    fputs("error: out of memory while scheduling\n", stderr);
    chart->failed = 1;
}

/*
 * Append the interval [start, end) for job_index to chart.
 * When merge_with_previous is non-zero and the last segment runs the same job
 * and ends exactly at start, the two intervals are merged instead.
 * Return 0 on success, -1 when the segment array cannot grow.
 */
static int chart_add(GanttChart *chart, int job_index, long long start, long long end,
                     int merge_with_previous)
{
    GanttSegment *seg;

    if (end <= start)
        return 0;
    if (merge_with_previous && chart->count > 0) {
        seg = &chart->segs[chart->count - 1];
        if (seg->job_index == job_index && seg->end == start) {
            seg->end = end;
            chart->finish = end;
            return 0;
        }
    }
    if (chart->count == chart->capacity) {
        GanttSegment *grown = grow_array(chart->segs, &chart->capacity, sizeof *grown);

        if (grown == NULL) {
            chart_out_of_memory(chart);
            return -1;
        }
        chart->segs = grown;
    }
    seg = &chart->segs[chart->count++];
    seg->job_index = job_index;
    seg->start = start;
    seg->end = end;
    chart->finish = end;
    return 0;
}

/* Advance *now to the next arrival and record the idle interval in chart.
 * Return -1 if recording fails, or 0 on success. */
static int advance_idle(GanttChart *chart, long long *now, long long arrival)
{
    if (chart_add(chart, NO_JOB, *now, arrival, 1) != 0)
        return -1;
    *now = arrival;
    return 0;
}

/* Return fresh burst times for queue, or NULL on allocation failure.
 * The caller owns the returned array. */
static int *copy_burst_times(const JobQueue *queue)
{
    int *remaining = calloc((size_t)queue->count, sizeof *remaining);

    if (remaining != NULL) {
        for (int i = 0; i < queue->count; ++i)
            remaining[i] = queue->jobs[i].burst_time;
    }
    return remaining;
}

/* ------------------------------------------------------------------ */
/* FCFS, SJF, and preemptive Priority                                  */
/* ------------------------------------------------------------------ */

typedef enum {
    BY_ARRIVAL,
    BY_BURST,
    BY_PRIORITY
} ReadyOrder;

/* Return 1 if candidate outranks incumbent under order. Equal jobs retain
 * their input order when the caller considers them from first to last. */
static int ready_job_precedes(const Job *candidate, const Job *incumbent, ReadyOrder order)
{
    if (order == BY_BURST && candidate->burst_time != incumbent->burst_time)
        return candidate->burst_time < incumbent->burst_time;
    if (order == BY_PRIORITY && candidate->priority != incumbent->priority)
        return candidate->priority < incumbent->priority;
    return candidate->arrival_time < incumbent->arrival_time;
}

/* Simulate queue's jobs using order. Append execution and idle intervals to
 * chart and mark it incomplete on allocation failure. */
static void schedule_ordered(const JobQueue *queue, GanttChart *chart, ReadyOrder order)
{
    if (queue->count == 0)
        return;

    int *remaining = copy_burst_times(queue);
    if (remaining == NULL) {
        chart_out_of_memory(chart);
        return;
    }

    int completed = 0;
    long long now = 0;
    while (completed < queue->count) {
        int best = NO_JOB;
        long long next_arrival = LLONG_MAX;

        for (int i = 0; i < queue->count; ++i) {
            const Job *candidate = &queue->jobs[i];

            if (remaining[i] == 0)
                continue;
            if (candidate->arrival_time > now) {
                if (candidate->arrival_time < next_arrival)
                    next_arrival = candidate->arrival_time;
            } else if (best == NO_JOB || ready_job_precedes(candidate, &queue->jobs[best], order)) {
                best = i;
            }
        }

        if (best == NO_JOB) {
            if (advance_idle(chart, &now, next_arrival) != 0)
                break;
            continue;
        }

        int run = remaining[best];
        if (order == BY_PRIORITY && next_arrival < now + run)
            run = (int)(next_arrival - now);
        if (chart_add(chart, best, now, now + run, 1) != 0)
            break;
        now += run;
        remaining[best] -= run;
        if (remaining[best] == 0)
            ++completed;
    }

    free(remaining);
}

/* Schedule queue's jobs non-preemptively by arrival time, then file order.
 * Append the schedule to chart, or mark it incomplete on allocation failure. */
void schedule_FCFS(JobQueue *queue, GanttChart *chart)
{
    schedule_ordered(queue, chart, BY_ARRIVAL);
}

/* Schedule queue's jobs non-preemptively by shortest burst, then arrival time.
 * Append the schedule to chart, or mark it incomplete on allocation failure. */
void schedule_SJF(JobQueue *queue, GanttChart *chart)
{
    schedule_ordered(queue, chart, BY_BURST);
}

/* Schedule queue's jobs by priority, then arrival time and file order.
 * Recheck priority at each arrival. Append intervals to chart, or mark it
 * incomplete on allocation failure. */
void schedule_priority_preemptive(JobQueue *queue, GanttChart *chart)
{
    schedule_ordered(queue, chart, BY_PRIORITY);
}

/* ------------------------------------------------------------------ */
/* Ready queues and pending arrivals for RR and MLFQ                   */
/* ------------------------------------------------------------------ */

typedef struct {
    int *jobs;
    size_t capacity;
    size_t head;
    size_t count;
} ReadyQueue;

/* Allocate room for job_count indices in ready. Return 0 on success or -1
 * on allocation failure. The caller owns ready's job array. */
static int ready_queue_init(ReadyQueue *ready, size_t job_count)
{
    *ready = (ReadyQueue){.jobs = calloc(job_count, sizeof *ready->jobs), .capacity = job_count};
    return ready->jobs != NULL ? 0 : -1;
}

/* Add job_index to the rear of ready, which must have space for it. */
static void ready_push(ReadyQueue *ready, int job_index)
{
    size_t tail = (ready->head + ready->count) % ready->capacity;

    ready->jobs[tail] = job_index;
    ++ready->count;
}

/* Remove and return the first job index from a nonempty ready queue. */
static int ready_pop(ReadyQueue *ready)
{
    int job_index = ready->jobs[ready->head];

    ready->head = (ready->head + 1) % ready->capacity;
    --ready->count;
    return job_index;
}

typedef struct {
    int time;
    int job_index;
} Arrival;

typedef struct {
    Arrival *entries;
    int count;
    int next;
} ArrivalList;

/* Compare two Arrival entries by time, then by file order. Return a
 * negative value, zero, or a positive value as required by qsort. */
static int compare_arrivals(const void *left, const void *right)
{
    const Arrival *first = left;
    const Arrival *second = right;

    if (first->time != second->time)
        return first->time < second->time ? -1 : 1;
    return (first->job_index > second->job_index) -
           (first->job_index < second->job_index);
}

/* Build an ordered list of arrivals from queue. Return 0 on success or -1
 * on allocation failure. The caller owns the list's entries. */
static int arrival_list_init(ArrivalList *arrivals, const JobQueue *queue)
{
    *arrivals = (ArrivalList){
        .entries = calloc((size_t)queue->count, sizeof *arrivals->entries),
        .count = queue->count
    };
    if (arrivals->entries == NULL)
        return -1;

    for (int i = 0; i < queue->count; ++i)
        arrivals->entries[i] = (Arrival){.time = queue->jobs[i].arrival_time, .job_index = i};
    qsort(arrivals->entries, (size_t)arrivals->count, sizeof *arrivals->entries, compare_arrivals);
    return 0;
}

/* Return the next pending arrival time, or LLONG_MAX after the last one. */
static long long next_arrival_time(const ArrivalList *arrivals)
{
    return arrivals->next < arrivals->count ? arrivals->entries[arrivals->next].time : LLONG_MAX;
}

/* Move arrivals up to and including now from the list into ready in time
 * order. ready must have space for all newly arrived jobs. */
static void admit_arrivals(ArrivalList *arrivals, ReadyQueue *ready, long long now)
{
    while (arrivals->next < arrivals->count && arrivals->entries[arrivals->next].time <= now)
        ready_push(ready, arrivals->entries[arrivals->next++].job_index);
}

/* Schedule queue's jobs with time_quantum and append each dispatch to chart.
 * Arrivals at the quantum boundary precede the requeued job. A nonpositive
 * quantum leaves chart alone. Allocation failure marks it incomplete. */
void schedule_RR(JobQueue *queue, int time_quantum, GanttChart *chart)
{
    if (time_quantum <= 0 || queue->count == 0)
        return;

    int *remaining = copy_burst_times(queue);
    ArrivalList arrivals = {0};
    ReadyQueue ready = {0};
    if (remaining == NULL || arrival_list_init(&arrivals, queue) != 0 ||
        ready_queue_init(&ready, (size_t)queue->count) != 0) {
        chart_out_of_memory(chart);
        goto done;
    }

    int completed = 0;
    long long now = 0;
    while (completed < queue->count) {
        admit_arrivals(&arrivals, &ready, now);
        if (ready.count == 0) {
            if (advance_idle(chart, &now, next_arrival_time(&arrivals)) != 0)
                break;
            continue;
        }

        int job = ready_pop(&ready);
        int run = remaining[job] < time_quantum ? remaining[job] : time_quantum;
        if (chart_add(chart, job, now, now + run, 0) != 0)
            break;
        now += run;
        remaining[job] -= run;

        if (remaining[job] > 0) {
            admit_arrivals(&arrivals, &ready, now);
            ready_push(&ready, job);
        } else {
            ++completed;
        }
    }

done:
    free(ready.jobs);
    free(arrivals.entries);
    free(remaining);
}

/* ------------------------------------------------------------------ */
/* Advanced part: three-level MLFQ                                    */
/* ------------------------------------------------------------------ */

enum {
    MLFQ_TOP_LEVEL,
    MLFQ_SECOND_LEVEL,
    MLFQ_FINAL_LEVEL,
    MLFQ_LEVEL_COUNT
};

enum {
    MLFQ_TOP_QUANTUM = 4,
    MLFQ_SECOND_QUANTUM = 8
};

static const int mlfq_quanta[MLFQ_LEVEL_COUNT] = {
    [MLFQ_TOP_LEVEL] = MLFQ_TOP_QUANTUM,
    [MLFQ_SECOND_LEVEL] = MLFQ_SECOND_QUANTUM
};

typedef struct {
    int remaining;
    int level;
    int used_at_level;
} MlfqJob;

/* Remove and return the first job at the highest ready level in queues.
 * Return NO_JOB when all levels are empty. */
static int mlfq_dequeue(ReadyQueue queues[MLFQ_LEVEL_COUNT])
{
    for (int level = MLFQ_TOP_LEVEL; level < MLFQ_LEVEL_COUNT; ++level) {
        if (queues[level].count > 0)
            return ready_pop(&queues[level]);
    }
    return NO_JOB;
}

/* Schedule queue's jobs across two RR levels and a final FCFS level. New
 * jobs enter the top level. Append intervals to chart, or mark it incomplete
 * on allocation failure. */
void schedule_MLFQ(JobQueue *queue, GanttChart *chart)
{
    if (queue->count == 0)
        return;

    ArrivalList arrivals = {0};
    ReadyQueue queues[MLFQ_LEVEL_COUNT] = {0};
    MlfqJob *jobs = calloc((size_t)queue->count, sizeof *jobs);
    if (jobs == NULL || arrival_list_init(&arrivals, queue) != 0) {
        chart_out_of_memory(chart);
        goto done;
    }
    for (int level = MLFQ_TOP_LEVEL; level < MLFQ_LEVEL_COUNT; ++level) {
        if (ready_queue_init(&queues[level], (size_t)queue->count) != 0) {
            chart_out_of_memory(chart);
            goto done;
        }
    }
    for (int i = 0; i < queue->count; ++i)
        jobs[i].remaining = queue->jobs[i].burst_time;

    int completed = 0;
    long long now = 0;
    while (completed < queue->count) {
        admit_arrivals(&arrivals, &queues[MLFQ_TOP_LEVEL], now);

        int job = mlfq_dequeue(queues);
        if (job == NO_JOB) {
            if (advance_idle(chart, &now, next_arrival_time(&arrivals)) != 0)
                break;
            continue;
        }

        MlfqJob *state = &jobs[job];
        int level = state->level;
        int run = state->remaining;
        if (level != MLFQ_FINAL_LEVEL) {
            int budget = mlfq_quanta[level] - state->used_at_level;
            if (run > budget)
                run = budget;
        }
        long long next = next_arrival_time(&arrivals);
        if (level == MLFQ_SECOND_LEVEL && next < now + run)
            run = (int)(next - now);

        if (chart_add(chart, job, now, now + run, 1) != 0)
            break;
        now += run;
        state->remaining -= run;
        state->used_at_level += run;

        if (state->remaining == 0) {
            ++completed;
        } else {
            /* Demote before admitting arrivals at the quantum boundary. */
            if (state->used_at_level == mlfq_quanta[level]) {
                ++state->level;
                state->used_at_level = 0;
            }
            ready_push(&queues[state->level], job);
        }
    }

done:
    for (int level = MLFQ_TOP_LEVEL; level < MLFQ_LEVEL_COUNT; ++level)
        free(queues[level].jobs);
    free(arrivals.entries);
    free(jobs);
}

/* ------------------------------------------------------------------ */
/* Metrics                                                            */
/* ------------------------------------------------------------------ */

/*
 * Compute per-job times, averages, and CPU utilisation from the completed
 * chart and its input queue, including idle time from time 0. Fill result
 * with metrics whose row array the caller must free. A NULL row array
 * means zero jobs or an allocation failure reported to stderr.
 */
void compute_metrics(const GanttChart *chart, const JobQueue *queue, Metrics *result)
{
    long double total_tat = 0;
    long double total_wt = 0;
    long long total_burst = 0;

    *result = (Metrics){0};
    if (queue->count == 0)
        return;

    result->rows = calloc((size_t)queue->count, sizeof *result->rows);
    if (result->rows == NULL) {
        fputs("error: out of memory while computing metrics\n", stderr);
        return;
    }

    for (size_t s = 0; s < chart->count; ++s) {
        int job = chart->segs[s].job_index;

        if (job != NO_JOB)
            result->rows[job].completion = chart->segs[s].end;
    }
    for (int i = 0; i < queue->count; ++i) {
        const Job *job = &queue->jobs[i];
        JobMetrics *row = &result->rows[i];

        row->turnaround = row->completion - job->arrival_time;
        row->waiting = row->turnaround - job->burst_time;
        total_tat += row->turnaround;
        total_wt += row->waiting;
        total_burst += job->burst_time;
    }

    result->avg_tat = (double)(total_tat / queue->count);
    result->avg_wt = (double)(total_wt / queue->count);
    if (chart->finish > 0)
        result->cpu_util = 100.0 * (double)total_burst / (double)chart->finish;
}

/* ------------------------------------------------------------------ */
/* Output                                                             */
/* ------------------------------------------------------------------ */

enum { TABLE_COLUMN_GAP = 2 };

/* Write count spaces to stdout. */
static void print_spaces(size_t count)
{
    for (size_t i = 0; i < count; ++i)
        putchar(' ');
}

/* Write text to stdout, padded with spaces to width bytes. */
static void print_padded_text(const char *text, size_t width)
{
    fputs(text, stdout);
    print_spaces(width - strlen(text));
}

/* Return the number of decimal digits needed to print a non-negative value. */
static int digits_in(long long value)
{
    int digits = 1;

    while (value >= 10) {
        value /= 10;
        ++digits;
    }
    return digits;
}

/* Return the width of a Gantt job label and write it to stdout when emit is set.
 * Quote IDs that could be mistaken for idle time or chart syntax. */
static size_t gantt_job_label(const char *job_id, int emit)
{
    size_t width = strlen(job_id);

    if (strcmp(job_id, IDLE_LABEL) != 0 && strpbrk(job_id, "|\"") == NULL) {
        if (emit)
            fputs(job_id, stdout);
        return width;
    }

    if (emit)
        putchar('"');
    for (const char *character = job_id; *character != '\0'; ++character) {
        if (*character == '"') {
            ++width;
            if (emit)
                putchar('"');
        }
        if (emit)
            putchar((unsigned char)*character);
    }
    if (emit)
        putchar('"');
    return width + 2;
}

/* Print chart's segment labels and aligned time boundaries to stdout. */
static void print_gantt(const GanttChart *chart)
{
    size_t label_width = sizeof IDLE_LABEL - 1;

    for (size_t i = 0; i < chart->count; ++i) {
        int job = chart->segs[i].job_index;

        if (job != NO_JOB) {
            size_t width = gantt_job_label(chart->jobs[job].job_id, 0);
            if (width > label_width)
                label_width = width;
        }
    }
    size_t time_width = (size_t)digits_in(chart->finish) + 1;
    if (label_width < time_width)
        label_width = time_width;
    size_t cell_width = label_width + sizeof "|  " - 1;

    fputs("Gantt: ", stdout);
    for (size_t i = 0; i < chart->count; ++i) {
        int job = chart->segs[i].job_index;

        fputs("| ", stdout);
        if (job == NO_JOB) {
            print_padded_text(IDLE_LABEL, label_width);
        } else {
            size_t width = gantt_job_label(chart->jobs[job].job_id, 1);
            print_spaces(label_width - width);
        }
        putchar(' ');
    }
    puts("|");

    fputs("Time:  ", stdout);
    for (size_t i = 0; i < chart->count; ++i) {
        long long start = chart->segs[i].start;
        printf("%lld", start);
        print_spaces(cell_width - (size_t)digits_in(start));
    }
    printf("%lld\n", chart->finish);
}

/* Return the greater of width and the columns needed for value and padding. */
static int table_column_width(int width, long long value)
{
    int needed = digits_in(value) + TABLE_COLUMN_GAP;
    return needed > width ? needed : width;
}

/* Print chart's jobs and their result rows in input order to stdout. */
static void print_table(const GanttChart *chart, const Metrics *result)
{
    size_t id_width = sizeof "Job" - 1 + TABLE_COLUMN_GAP;
    int at_width = sizeof "AT" - 1 + TABLE_COLUMN_GAP;
    int bt_width = sizeof "BT" - 1 + TABLE_COLUMN_GAP;
    int ct_width = sizeof "CT" - 1 + TABLE_COLUMN_GAP;
    int tat_width = sizeof "TAT" - 1 + TABLE_COLUMN_GAP;

    for (int i = 0; i < chart->job_count; ++i) {
        const Job *job = &chart->jobs[i];
        const JobMetrics *row = &result->rows[i];
        size_t job_width = strlen(job->job_id) + TABLE_COLUMN_GAP;

        if (job_width > id_width)
            id_width = job_width;
        at_width = table_column_width(at_width, job->arrival_time);
        bt_width = table_column_width(bt_width, job->burst_time);
        ct_width = table_column_width(ct_width, row->completion);
        tat_width = table_column_width(tat_width, row->turnaround);
    }

    print_padded_text("Job", id_width);
    printf("%-*s%-*s%-*s%-*s%s\n", at_width, "AT", bt_width, "BT",
           ct_width, "CT", tat_width, "TAT", "WT");
    for (int i = 0; i < chart->job_count; ++i) {
        const Job *job = &chart->jobs[i];
        const JobMetrics *row = &result->rows[i];

        print_padded_text(job->job_id, id_width);
        printf("%-*d%-*d%-*lld%-*lld%lld\n", at_width, job->arrival_time,
               bt_width, job->burst_time, ct_width, row->completion,
               tat_width, row->turnaround, row->waiting);
    }
}

/*
 * Print algo_name, chart, and the completed metrics in result to stdout.
 */
void print_schedule(const char *algo_name, const GanttChart *chart, const Metrics *result)
{
    printf("=== %s ===\n", algo_name);
    if (chart->count == 0) {
        printf("Gantt: (no jobs)\n");
        printf("Time:  0\n");
    } else {
        print_gantt(chart);
    }
    printf("\n");
    print_table(chart, result);
    printf("Avg TAT: %.2f   Avg WT: %.2f   CPU Util: %.2f%%\n",
           result->avg_tat, result->avg_wt, result->cpu_util);
}

/* Print the command line usage for program to stderr. */
static void print_usage(const char *program)
{
    fprintf(stderr, "usage: %s [--mlfq] <orders_file>\n", program);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                        */
/* ------------------------------------------------------------------ */

enum { RR_SHORT_QUANTUM = 3, RR_LONG_QUANTUM = 6 };

/* Use the named scheduler when present. Otherwise use the Round Robin quantum. */
typedef struct {
    const char *name;
    void (*schedule)(JobQueue *, GanttChart *);
    int quantum;
} Configuration;

static const Configuration required_configurations[] = {
    {.name = "FCFS", .schedule = schedule_FCFS},
    {.name = "SJF", .schedule = schedule_SJF},
    {.name = "Preemptive Priority", .schedule = schedule_priority_preemptive},
    {.quantum = RR_SHORT_QUANTUM},
    {.quantum = RR_LONG_QUANTUM}
};

static const Configuration mlfq_configuration = {.name = "MLFQ", .schedule = schedule_MLFQ};
static const char rr_name_format[] = "Round Robin (q=%d)";

/* Run config on queue and print its schedule and metrics. Release the run's
 * allocations and return 0 on success or -1 on a scheduling or metrics error. */
static int run_configuration(const Configuration *config, JobQueue *queue)
{
    GanttChart chart;
    Metrics metrics = {0};
    const char *name = config->name;
    char rr_name[sizeof rr_name_format + sizeof config->quantum * CHAR_BIT];
    int result = -1;

    chart_init(&chart, queue);
    if (config->schedule != NULL) {
        config->schedule(queue, &chart);
    } else {
        snprintf(rr_name, sizeof rr_name, rr_name_format, config->quantum);
        name = rr_name;
        schedule_RR(queue, config->quantum, &chart);
    }

    if (!chart.failed) {
        compute_metrics(&chart, queue, &metrics);
        if (metrics.rows != NULL || chart.job_count == 0) {
            print_schedule(name, &chart, &metrics);
            result = 0;
        }
    }
    free(metrics.rows);
    chart_release(&chart);
    return result;
}

/*
 * Read the orders-file path and optional --mlfq flag from argv. Print the
 * required configurations in order, then MLFQ when requested.
 * Return EXIT_SUCCESS, or EXIT_FAILURE on bad arguments, unreadable input or
 * an allocation failure.
 */
int main(int argc, char **argv)
{
    const char *path = NULL;
    int with_mlfq = 0;
    JobQueue queue;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--mlfq") == 0) {
            with_mlfq = 1;
        } else if (path == NULL) {
            path = argv[i];
        } else {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }
    if (path == NULL) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (load_jobs(path, &queue) < 0)
        return EXIT_FAILURE;

    int status = EXIT_FAILURE;
    size_t required_count = sizeof required_configurations / sizeof *required_configurations;
    for (size_t i = 0; i < required_count; ++i) {
        if (i > 0)
            putchar('\n');
        if (run_configuration(&required_configurations[i], &queue) != 0)
            goto done;
    }

    if (with_mlfq) {
        putchar('\n');
        if (run_configuration(&mlfq_configuration, &queue) != 0)
            goto done;
    }
    status = EXIT_SUCCESS;

done:
    queue_destroy(&queue);
    return status;
}
