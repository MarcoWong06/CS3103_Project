Q3 sample inputs

Every supplied file begins with this header, matching the document:
  job_id arrival burst priority
The header is not a job. Subsequent lines contain the four job fields.
Sample arrivals are nonnegative, bursts are positive, and priorities are
1 through 5 (1 is most urgent). Job IDs are unique within each sample.

After implementing problem3.c, run, for example:
  ./problem3 test_cases/q3/case1.txt

Each invocation runs the five configurations in the order stated in the
specification: FCFS, SJF, preemptive Priority, RR q=3, RR q=6. Base scheduling
on the input arrival and burst times, as described in the document.

case1.txt  The document's five jobs; compare with your hand calculations.
case2.txt  Idle periods between groups of arrivals.
case3.txt  Equal burst times; apply each algorithm's stated selection rules.
case4.txt  A long low-priority job with later higher-priority arrivals.
           Check preemption and eventual completion of the long job.
case5.txt  Extra example with simultaneous arrivals and different bursts.

Apply only the tie-breaking rules stated in the specification. These samples
do not introduce an extra rule for events whose ordering is left unspecified.
These are sample inputs, not a complete grading suite. Follow the document
for required report calculations, tests and the optional MLFQ extension.
