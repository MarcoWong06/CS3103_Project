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

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_JOBS 512            /* capacity of the job table */
#define JOB_ID_MAX 32           /* bytes reserved for one job id, including '\0' */
#define MIN_PRIORITY 1          /* most urgent priority value */
#define MAX_PRIORITY 5          /* least urgent priority value */
#define IDLE_JOB (-1)           /* job index used for an idle Gantt segment */
#define INITIAL_CAPACITY 32     /* first allocation of the job and segment arrays */
#define MLFQ_LAST_LEVEL 2       /* index of the last (FCFS) MLFQ level */
#define MLFQ_QUANTUM_LEVEL1 4   /* time quantum of MLFQ level 1 */
#define MLFQ_QUANTUM_LEVEL2 8   /* time quantum of MLFQ level 2 */

/* One input job, stored in the order in which it appeared in the file. */
typedef struct {
    char job_id[JOB_ID_MAX];
    int arrival_time;
    int burst_time;
    int priority;
} Job;

/* All jobs of one input file, in input-file order. */
typedef struct {
    Job *jobs;
    int count;
    int capacity;
} JobQueue;

/* One contiguous interval in which the CPU runs the same job. */
typedef struct {
    int job_index;              /* IDLE_JOB for an idle interval */
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
    int count;
    int capacity;
    long long finish;           /* end of the last segment */
    const Job *jobs;            /* borrowed from the job queue */
    int job_count;
    int failed;                 /* non-zero when a segment allocation failed */
} GanttChart;

/* Per-job and aggregate metrics of one scheduling configuration. */
typedef struct {
    long long ct[MAX_JOBS];
    long long tat[MAX_JOBS];
    long long wt[MAX_JOBS];
    double avg_tat;
    double avg_wt;
    double cpu_util;
    long long finish_time;
    long long total_burst;
    int job_count;
} Metrics;

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

/* Return 1 when c is an ASCII white-space character. */
static int is_space_char(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

/* Return 1 when the token [text, text + len) equals word, ignoring case. */
static int token_matches_ignore_case(const char *text, size_t len, const char *word)
{
    size_t i;

    if (strlen(word) != len)
        return 0;
    for (i = 0; i < len; ++i) {
        char a = text[i];
        char b = word[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');
        if (a != b)
            return 0;
    }
    return 1;
}

/*
 * Parse one base-10 integer at *cursor and advance *cursor past it.
 * Return 0 on success, -1 when no integer is present or the value overflows.
 */
static int parse_long_field(const char **cursor, long *value)
{
    const char *p = *cursor;
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(p, &end, 10);
    if (end == p || errno == ERANGE)
        return -1;
    *value = parsed;
    *cursor = end;
    return 0;
}

/*
 * Parse one job line of the documented input format, that is
 * "job_id arrival burst priority".
 * Return 0 on success, -1 when the line is malformed or a field is out of range.
 */
static int parse_job_line(const char *line, Job *job)
{
    const char *p = line;
    size_t id_len = 0;
    long values[3];
    int field;

    while (p[id_len] != '\0' && !is_space_char((unsigned char)p[id_len]))
        ++id_len;
    if (id_len == 0 || id_len >= JOB_ID_MAX)
        return -1;

    memcpy(job->job_id, p, id_len);
    job->job_id[id_len] = '\0';
    p += id_len;

    for (field = 0; field < 3; ++field) {
        while (is_space_char((unsigned char)*p))
            ++p;
        if (parse_long_field(&p, &values[field]) != 0)
            return -1;
    }
    while (is_space_char((unsigned char)*p))
        ++p;
    if (*p != '\0')
        return -1;                                      /* trailing garbage */

    if (values[0] < 0 || values[0] > INT_MAX)           /* arrival time */
        return -1;
    if (values[1] <= 0 || values[1] > INT_MAX)          /* burst time */
        return -1;
    if (values[2] < MIN_PRIORITY || values[2] > MAX_PRIORITY)  /* priority */
        return -1;

    job->arrival_time = (int)values[0];
    job->burst_time = (int)values[1];
    job->priority = (int)values[2];
    return 0;
}

/*
 * Read the job file into queue, preserving input order.
 * Return the number of jobs read, or -1 on any error.  Errors are reported on
 * stderr, and a failed call leaves an empty queue behind.
 */
int load_jobs(const char *filepath, JobQueue *queue)
{
    FILE *fp;
    char line[512];
    int line_number = 0;
    int seen_content = 0;

    queue->jobs = NULL;
    queue->count = 0;
    queue->capacity = 0;

    if (filepath == NULL) {
        fprintf(stderr, "error: no input file given\n");
        return -1;
    }
    fp = fopen(filepath, "r");
    if (fp == NULL) {
        fprintf(stderr, "error: cannot open '%s': %s\n", filepath, strerror(errno));
        return -1;
    }

    while (fgets(line, (int)sizeof line, fp) != NULL) {
        char *p = line;
        size_t len = strlen(line);
        Job job;

        ++line_number;
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';                         /* accept CRLF files */
        while (is_space_char((unsigned char)*p))
            ++p;
        if (*p == '\0')
            continue;                                   /* blank line */

        if (!seen_content) {
            const char *token = p;
            size_t token_len = 0;
            seen_content = 1;
            while (token[token_len] != '\0' &&
                   !is_space_char((unsigned char)token[token_len]))
                ++token_len;
            if (token_matches_ignore_case(token, token_len, "job_id"))
                continue;                               /* documented header */
            /* No header: this line is the first job. */
        }

        if (parse_job_line(p, &job) != 0) {
            fprintf(stderr,
                    "error: %s: line %d: expected 'job_id arrival burst priority' "
                    "with arrival >= 0, burst > 0 and priority %d..%d\n",
                    filepath, line_number, MIN_PRIORITY, MAX_PRIORITY);
            fclose(fp);
            free(queue->jobs);
            queue->jobs = NULL;
            queue->count = 0;
            queue->capacity = 0;
            return -1;
        }

        if (queue->count == MAX_JOBS) {
            fprintf(stderr, "error: %s: more than %d jobs are not supported\n",
                    filepath, MAX_JOBS);
            fclose(fp);
            free(queue->jobs);
            queue->jobs = NULL;
            queue->count = 0;
            queue->capacity = 0;
            return -1;
        }
        if (queue->count == queue->capacity) {
            int new_capacity = queue->capacity ? queue->capacity * 2 : INITIAL_CAPACITY;
            Job *grown;

            if (new_capacity > MAX_JOBS)
                new_capacity = MAX_JOBS;
            grown = (Job *)realloc(queue->jobs, (size_t)new_capacity * sizeof *grown);
            if (grown == NULL) {
                fprintf(stderr, "error: out of memory while reading '%s'\n", filepath);
                fclose(fp);
                free(queue->jobs);
                queue->jobs = NULL;
                queue->count = 0;
                queue->capacity = 0;
                return -1;
            }
            queue->jobs = grown;
            queue->capacity = new_capacity;
        }
        queue->jobs[queue->count++] = job;
    }

    fclose(fp);
    return queue->count;
}

/* Release the job array of a queue filled in by load_jobs(). */
static void queue_destroy(JobQueue *queue)
{
    free(queue->jobs);
    queue->jobs = NULL;
    queue->count = 0;
    queue->capacity = 0;
}

/* ------------------------------------------------------------------ */
/* Gantt chart                                                        */
/* ------------------------------------------------------------------ */

/* Prepare an empty chart that borrows the job table of queue. */
static void chart_init(GanttChart *chart, const JobQueue *queue)
{
    chart->segs = NULL;
    chart->count = 0;
    chart->capacity = 0;
    chart->finish = 0;
    chart->jobs = queue->jobs;
    chart->job_count = queue->count;
    chart->failed = 0;
}

/* Release the segment array of a chart. */
static void chart_release(GanttChart *chart)
{
    free(chart->segs);
    chart->segs = NULL;
    chart->count = 0;
    chart->capacity = 0;
}

/*
 * Append the interval [start, end) to the chart.
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
        int new_capacity = chart->capacity ? chart->capacity * 2 : INITIAL_CAPACITY;
        GanttSegment *grown = (GanttSegment *)realloc(
            chart->segs, (size_t)new_capacity * sizeof *grown);

        if (grown == NULL) {
            fprintf(stderr, "error: out of memory while recording the schedule\n");
            chart->failed = 1;
            return -1;
        }
        chart->segs = grown;
        chart->capacity = new_capacity;
    }
    seg = &chart->segs[chart->count++];
    seg->job_index = job_index;
    seg->start = start;
    seg->end = end;
    chart->finish = end;
    return 0;
}

/*
 * Return the earliest arrival time strictly after now among the jobs that
 * still have work left, or -1 when no such arrival exists.
 */
static long long next_arrival_after(const Job *jobs, const long long *remaining,
                                    int count, long long now)
{
    long long best = -1;
    int i;

    for (i = 0; i < count; ++i) {
        if (remaining[i] <= 0 || jobs[i].arrival_time <= now)
            continue;
        if (best < 0 || (long long)jobs[i].arrival_time < best)
            best = jobs[i].arrival_time;
    }
    return best;
}

/*
 * Return the earliest arrival time among jobs that have not been admitted to a
 * ready queue yet, or -1 when every job has already arrived.
 */
static long long earliest_unarrived(const Job *jobs, const unsigned char *arrived,
                                    int count)
{
    long long best = -1;
    int i;

    for (i = 0; i < count; ++i) {
        if (arrived[i])
            continue;
        if (best < 0 || (long long)jobs[i].arrival_time < best)
            best = jobs[i].arrival_time;
    }
    return best;
}

/* ------------------------------------------------------------------ */
/* Scheduling algorithms                                              */
/* ------------------------------------------------------------------ */

/*
 * FCFS: run the ready job with the earliest arrival time, breaking ties by
 * input position.  Non-preemptive; every idle gap becomes an IDLE segment.
 */
void schedule_FCFS(JobQueue *queue, GanttChart *chart)
{
    long long remaining[MAX_JOBS];
    int count = queue->count;
    int completed = 0;
    int i;
    long long now = 0;

    for (i = 0; i < count; ++i)
        remaining[i] = queue->jobs[i].burst_time;

    while (completed < count) {
        int best = -1;

        for (i = 0; i < count; ++i) {
            if (remaining[i] <= 0 || queue->jobs[i].arrival_time > now)
                continue;
            if (best < 0 ||
                queue->jobs[i].arrival_time < queue->jobs[best].arrival_time ||
                (queue->jobs[i].arrival_time == queue->jobs[best].arrival_time && i < best))
                best = i;
        }

        if (best < 0) {
            long long next = next_arrival_after(queue->jobs, remaining, count, now);

            if (next < 0)
                break;
            if (chart_add(chart, IDLE_JOB, now, next, 1) != 0)
                return;
            now = next;
            continue;
        }

        if (chart_add(chart, best, now, now + remaining[best], 1) != 0)
            return;
        now += remaining[best];
        remaining[best] = 0;
        ++completed;
    }
}

/*
 * SJF: run the ready job with the shortest burst time, breaking ties by
 * arrival time and then by input position.  Non-preemptive.
 */
void schedule_SJF(JobQueue *queue, GanttChart *chart)
{
    long long remaining[MAX_JOBS];
    int count = queue->count;
    int completed = 0;
    int i;
    long long now = 0;

    for (i = 0; i < count; ++i)
        remaining[i] = queue->jobs[i].burst_time;

    while (completed < count) {
        int best = -1;

        for (i = 0; i < count; ++i) {
            if (remaining[i] <= 0 || queue->jobs[i].arrival_time > now)
                continue;
            if (best < 0 ||
                queue->jobs[i].burst_time < queue->jobs[best].burst_time ||
                (queue->jobs[i].burst_time == queue->jobs[best].burst_time &&
                 queue->jobs[i].arrival_time < queue->jobs[best].arrival_time) ||
                (queue->jobs[i].burst_time == queue->jobs[best].burst_time &&
                 queue->jobs[i].arrival_time == queue->jobs[best].arrival_time && i < best))
                best = i;
        }

        if (best < 0) {
            long long next = next_arrival_after(queue->jobs, remaining, count, now);

            if (next < 0)
                break;
            if (chart_add(chart, IDLE_JOB, now, next, 1) != 0)
                return;
            now = next;
            continue;
        }

        if (chart_add(chart, best, now, now + remaining[best], 1) != 0)
            return;
        now += remaining[best];
        remaining[best] = 0;
        ++completed;
    }
}

/*
 * Preemptive Priority: run the ready job with the smallest priority value
 * (1 is the most urgent), breaking ties by arrival time and then by input
 * position.  The dispatcher re-evaluates at every arrival instant, so a newly
 * arrived more urgent job preempts the running one immediately.
 */
void schedule_priority_preemptive(JobQueue *queue, GanttChart *chart)
{
    long long remaining[MAX_JOBS];
    int count = queue->count;
    int completed = 0;
    int i;
    long long now = 0;

    for (i = 0; i < count; ++i)
        remaining[i] = queue->jobs[i].burst_time;

    while (completed < count) {
        int best = -1;

        for (i = 0; i < count; ++i) {
            if (remaining[i] <= 0 || queue->jobs[i].arrival_time > now)
                continue;
            if (best < 0 ||
                queue->jobs[i].priority < queue->jobs[best].priority ||
                (queue->jobs[i].priority == queue->jobs[best].priority &&
                 queue->jobs[i].arrival_time < queue->jobs[best].arrival_time) ||
                (queue->jobs[i].priority == queue->jobs[best].priority &&
                 queue->jobs[i].arrival_time == queue->jobs[best].arrival_time && i < best))
                best = i;
        }

        if (best < 0) {
            long long next = next_arrival_after(queue->jobs, remaining, count, now);

            if (next < 0)
                break;
            if (chart_add(chart, IDLE_JOB, now, next, 1) != 0)
                return;
            now = next;
            continue;
        }

        {
            long long run = remaining[best];
            long long next = next_arrival_after(queue->jobs, remaining, count, now);

            if (next >= 0 && next - now < run)
                run = next - now;                       /* stop at the next arrival */
            if (chart_add(chart, best, now, now + run, 1) != 0)
                return;
            now += run;
            remaining[best] -= run;
            if (remaining[best] == 0)
                ++completed;
        }
    }
}

/*
 * Admit every job that has arrived by now into the Round Robin ready queue,
 * in (arrival time, input position) order.  Each job is admitted once.
 */
static int rr_admit_arrivals(const JobQueue *queue, const long long *remaining,
                             unsigned char *arrived, int *fifo, int *tail,
                             int *queued, long long now)
{
    int admitted = 0;

    for (;;) {
        int best = -1;
        int i;

        for (i = 0; i < queue->count; ++i) {
            if (arrived[i] || remaining[i] <= 0 ||
                queue->jobs[i].arrival_time > now)
                continue;
            if (best < 0 ||
                queue->jobs[i].arrival_time < queue->jobs[best].arrival_time ||
                (queue->jobs[i].arrival_time == queue->jobs[best].arrival_time && i < best))
                best = i;
        }
        if (best < 0)
            break;
        arrived[best] = 1;
        fifo[*tail] = best;
        *tail = (*tail + 1) % MAX_JOBS;
        ++*queued;
        ++admitted;
    }
    return admitted;
}

/*
 * Round Robin: FIFO ready queue.  Arrivals append in arrival order and a job
 * that uses its whole quantum rejoins at the rear.  Jobs arriving exactly when
 * a quantum expires are queued before the preempted job.  Every dispatch is a
 * separate segment, so the quantum stays visible in the Gantt chart.
 */
void schedule_RR(JobQueue *queue, int time_quantum, GanttChart *chart)
{
    long long remaining[MAX_JOBS] = {0};
    unsigned char arrived[MAX_JOBS] = {0};
    int fifo[MAX_JOBS];
    int head = 0;
    int tail = 0;
    int queued = 0;
    int count = queue->count;
    int completed = 0;
    int i;
    long long now = 0;

    if (time_quantum <= 0)
        return;

    for (i = 0; i < count; ++i) {
        remaining[i] = queue->jobs[i].burst_time;
        arrived[i] = 0;
    }

    for (;;) {
        int job;

        rr_admit_arrivals(queue, remaining, arrived, fifo, &tail, &queued, now);
        if (completed >= count)
            break;

        if (queued == 0) {
            long long next = earliest_unarrived(queue->jobs, arrived, count);

            if (next < 0)
                break;
            if (chart_add(chart, IDLE_JOB, now, next, 1) != 0)
                return;
            now = next;
            continue;
        }

        job = fifo[head];
        head = (head + 1) % MAX_JOBS;
        --queued;

        {
            long long run = remaining[job] < (long long)time_quantum
                                ? remaining[job]
                                : (long long)time_quantum;

            if (chart_add(chart, job, now, now + run, 0) != 0)
                return;
            now += run;
            remaining[job] -= run;
        }

        /* Arrivals at this instant join before the preempted job returns. */
        rr_admit_arrivals(queue, remaining, arrived, fifo, &tail, &queued, now);

        if (remaining[job] > 0) {
            fifo[tail] = job;
            tail = (tail + 1) % MAX_JOBS;
            ++queued;
        } else {
            ++completed;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Advanced part: three-level MLFQ                                    */
/* ------------------------------------------------------------------ */

/* Return the quantum of one MLFQ level (0 for the FCFS level). */
static int mlfq_quantum(int level)
{
    if (level == 0)
        return MLFQ_QUANTUM_LEVEL1;
    if (level == 1)
        return MLFQ_QUANTUM_LEVEL2;
    return 0;
}

/* Append a job to the FIFO of one MLFQ level. */
static void mlfq_enqueue(int queues[3][MAX_JOBS], int counts[3],
                         int tails[3], int level, int job)
{
    queues[level][tails[level]] = job;
    tails[level] = (tails[level] + 1) % MAX_JOBS;
    ++counts[level];
}

/* Pop the most urgent waiting job: level 1 first, then 2, then 3. */
static int mlfq_dequeue(int queues[3][MAX_JOBS], int counts[3], int heads[3])
{
    int level;

    for (level = 0; level <= MLFQ_LAST_LEVEL; ++level) {
        int job;

        if (counts[level] == 0)
            continue;
        job = queues[level][heads[level]];
        heads[level] = (heads[level] + 1) % MAX_JOBS;
        --counts[level];
        return job;
    }
    return -1;
}

/*
 * Three-level MLFQ: level 1 is RR with quantum 4, level 2 is RR with quantum 8,
 * level 3 is FCFS and runs a job to completion without preemption.  A job that
 * uses its whole quantum is demoted one level; new arrivals enter level 1.
 * Higher levels are always served first.  Simulation advances one time unit at
 * a time, which keeps preemption and demotion exact for small quanta.
 */
void schedule_MLFQ(JobQueue *queue, GanttChart *chart)
{
    long long remaining[MAX_JOBS] = {0};
    long long used[MAX_JOBS] = {0};           /* CPU used at the current level */
    int level_of[MAX_JOBS] = {0};
    unsigned char arrived[MAX_JOBS] = {0};
    int queues[3][MAX_JOBS];
    int counts[3] = {0, 0, 0};
    int heads[3] = {0, 0, 0};
    int tails[3] = {0, 0, 0};
    int count = queue->count;
    int completed = 0;
    int current = -1;
    int i;
    long long now = 0;

    for (i = 0; i < count; ++i) {
        remaining[i] = queue->jobs[i].burst_time;
        used[i] = 0;
        level_of[i] = 0;
        arrived[i] = 0;
    }

    while (completed < count) {
        int job;

        /* Newly arrived jobs always enter level 1, in arrival/input order. */
        for (;;) {
            int best = -1;

            for (i = 0; i < count; ++i) {
                if (arrived[i] || remaining[i] <= 0 ||
                    queue->jobs[i].arrival_time > now)
                    continue;
                if (best < 0 ||
                    queue->jobs[i].arrival_time < queue->jobs[best].arrival_time ||
                    (queue->jobs[i].arrival_time == queue->jobs[best].arrival_time && i < best))
                    best = i;
            }
            if (best < 0)
                break;
            arrived[best] = 1;
            mlfq_enqueue(queues, counts, tails, 0, best);
        }

        if (current < 0) {
            current = mlfq_dequeue(queues, counts, heads);
            if (current < 0) {
                long long next = earliest_unarrived(queue->jobs, arrived, count);

                if (next < 0)
                    break;
                if (chart_add(chart, IDLE_JOB, now, next, 1) != 0)
                    return;
                now = next;
                continue;
            }
        }

        if (level_of[current] != MLFQ_LAST_LEVEL) {
            int level = level_of[current];
            int higher_ready = 0;
            int k;

            for (k = 0; k < level; ++k) {
                if (counts[k] > 0)
                    higher_ready = 1;
            }

            if (higher_ready) {
                /* A more urgent level has work: give up the CPU at once. */
                mlfq_enqueue(queues, counts, tails, level, current);
                current = mlfq_dequeue(queues, counts, heads);
            } else if (used[current] >= (long long)mlfq_quantum(level)) {
                /* Quantum exhausted: demote one level and re-dispatch. */
                ++level_of[current];
                used[current] = 0;
                mlfq_enqueue(queues, counts, tails, level_of[current], current);
                current = mlfq_dequeue(queues, counts, heads);
            }
        }

        job = current;
        if (chart_add(chart, job, now, now + 1, 1) != 0)
            return;
        --remaining[job];
        ++used[job];
        ++now;
        if (remaining[job] == 0) {
            ++completed;
            current = -1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Metrics                                                            */
/* ------------------------------------------------------------------ */

/*
 * Compute completion, turnaround and waiting times for every job, plus the
 * averages and the CPU utilisation of the whole run.
 * CPU utilisation is 100 * total burst / final completion time, so idle time
 * from time 0 is included in the denominator.
 */
void compute_metrics(const GanttChart *chart, const JobQueue *queue, Metrics *result)
{
    int count = queue->count;
    int i;
    int s;
    long long total_tat = 0;
    long long total_wt = 0;
    long long total_burst = 0;

    memset(result, 0, sizeof *result);
    result->job_count = count;
    result->finish_time = chart->finish;

    for (i = 0; i < count; ++i) {
        result->ct[i] = -1;
        total_burst += queue->jobs[i].burst_time;
    }
    for (s = 0; s < chart->count; ++s) {
        int job = chart->segs[s].job_index;

        if (job >= 0 && job < count)
            result->ct[job] = chart->segs[s].end;
    }
    for (i = 0; i < count; ++i) {
        if (result->ct[i] < 0)
            result->ct[i] = result->finish_time;
        result->tat[i] = result->ct[i] - (long long)queue->jobs[i].arrival_time;
        result->wt[i] = result->tat[i] - (long long)queue->jobs[i].burst_time;
        total_tat += result->tat[i];
        total_wt += result->wt[i];
    }

    result->total_burst = total_burst;
    if (count > 0) {
        result->avg_tat = (double)total_tat / (double)count;
        result->avg_wt = (double)total_wt / (double)count;
    }
    if (result->finish_time > 0 && total_burst > 0)
        result->cpu_util = 100.0 * (double)total_burst / (double)result->finish_time;
    else
        result->cpu_util = 0.0;
}

/* ------------------------------------------------------------------ */
/* Output                                                             */
/* ------------------------------------------------------------------ */

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

/* Print the "Gantt:" and "Time:" lines of one configuration. */
static void print_gantt(const GanttChart *chart)
{
    int label_width = 4;                /* "IDLE" needs four columns */
    int cell_width;
    int i;
    int column;

    for (i = 0; i < chart->count; ++i) {
        int job = chart->segs[i].job_index;
        int len = 4;

        if (job >= 0)
            len = (int)strlen(chart->jobs[job].job_id);
        if (len > label_width)
            label_width = len;
    }
    /* Keep at least one space between two consecutive time values. */
    if (label_width < digits_in(chart->finish) + 1)
        label_width = digits_in(chart->finish) + 1;
    cell_width = label_width + 3;       /* "| " + label + " " */

    printf("Gantt: ");
    for (i = 0; i < chart->count; ++i) {
        int job = chart->segs[i].job_index;

        if (job >= 0)
            printf("| %-*s ", label_width, chart->jobs[job].job_id);
        else
            printf("| %-*s ", label_width, "IDLE");
    }
    printf("|\n");

    printf("Time:");
    column = 5;                         /* strlen("Time:") */
    for (i = 0; i <= chart->count; ++i) {
        int target = 7 + i * cell_width;    /* strlen("Gantt: ") */
        long long value = (i < chart->count) ? chart->segs[i].start : chart->finish;
        char buffer[32];
        int len;

        if (target > column) {
            printf("%*s", target - column, "");
            column = target;
        }
        len = snprintf(buffer, sizeof buffer, "%lld", value);
        if (len < 0)
            len = 0;
        printf("%s", buffer);
        column += len;
    }
    printf("\n");
}

/* Print the per-job table of one configuration. */
static void print_table(const GanttChart *chart, const Metrics *result)
{
    int id_width = 5;                   /* "Job" plus at least two spaces */
    int at_width = 4;
    int bt_width = 4;
    int ct_width = 4;
    int tat_width = 5;
    int i;

    for (i = 0; i < chart->job_count; ++i) {
        int id_len = (int)strlen(chart->jobs[i].job_id);

        if (id_len + 2 > id_width)
            id_width = id_len + 2;
        if (digits_in(chart->jobs[i].arrival_time) + 2 > at_width)
            at_width = digits_in(chart->jobs[i].arrival_time) + 2;
        if (digits_in(chart->jobs[i].burst_time) + 2 > bt_width)
            bt_width = digits_in(chart->jobs[i].burst_time) + 2;
        if (digits_in(result->ct[i]) + 2 > ct_width)
            ct_width = digits_in(result->ct[i]) + 2;
        if (digits_in(result->tat[i]) + 2 > tat_width)
            tat_width = digits_in(result->tat[i]) + 2;
    }

    printf("%-*s%-*s%-*s%-*s%-*s%s\n", id_width, "Job", at_width, "AT",
           bt_width, "BT", ct_width, "CT", tat_width, "TAT", "WT");
    for (i = 0; i < chart->job_count; ++i) {
        printf("%-*s%-*lld%-*lld%-*lld%-*lld%lld\n", id_width,
               chart->jobs[i].job_id, at_width,
               (long long)chart->jobs[i].arrival_time, bt_width,
               (long long)chart->jobs[i].burst_time, ct_width,
               result->ct[i], tat_width, result->tat[i], result->wt[i]);
    }
}

/*
 * Print one configuration block: Gantt chart, per-job metrics and averages.
 * The five required configuration names are chosen by main().
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

/* Print the command line usage. */
static void print_usage(const char *program)
{
    fprintf(stderr, "usage: %s [--mlfq] <orders_file>\n", program);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                        */
/* ------------------------------------------------------------------ */

/*
 * Run one configuration, compute its metrics and print its block.
 * Return 0 on success, -1 when the schedule could not be recorded.
 */
static int run_configuration(int index, JobQueue *queue, Metrics *metrics)
{
    static const char *const names[5] = {
        "FCFS", "SJF", "Preemptive Priority", "Round Robin (q=3)", "Round Robin (q=6)"
    };
    GanttChart chart;

    chart_init(&chart, queue);
    switch (index) {
    case 0:
        schedule_FCFS(queue, &chart);
        break;
    case 1:
        schedule_SJF(queue, &chart);
        break;
    case 2:
        schedule_priority_preemptive(queue, &chart);
        break;
    case 3:
        schedule_RR(queue, 3, &chart);
        break;
    default:
        schedule_RR(queue, 6, &chart);
        break;
    }
    if (chart.failed) {
        chart_release(&chart);
        return -1;
    }
    compute_metrics(&chart, queue, metrics);
    print_schedule(names[index], &chart, metrics);
    chart_release(&chart);
    return 0;
}

/*
 * Run the five required configurations in order and print their blocks.
 * With --mlfq, append the optional MLFQ block.
 * Return EXIT_SUCCESS, or EXIT_FAILURE on bad arguments, unreadable input or
 * an allocation failure.
 */
int main(int argc, char **argv)
{
    const char *path = NULL;
    int with_mlfq = 0;
    JobQueue queue;
    Metrics metrics;
    int i;

    for (i = 1; i < argc; ++i) {
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

    for (i = 0; i < 5; ++i) {
        if (i > 0)
            printf("\n");
        if (run_configuration(i, &queue, &metrics) != 0) {
            queue_destroy(&queue);
            return EXIT_FAILURE;
        }
    }

    if (with_mlfq) {
        GanttChart chart;

        chart_init(&chart, &queue);
        schedule_MLFQ(&queue, &chart);
        if (chart.failed) {
            chart_release(&chart);
            queue_destroy(&queue);
            return EXIT_FAILURE;
        }
        compute_metrics(&chart, &queue, &metrics);
        printf("\n");
        print_schedule("MLFQ", &chart, &metrics);
        chart_release(&chart);
    }

    queue_destroy(&queue);
    return EXIT_SUCCESS;
}
