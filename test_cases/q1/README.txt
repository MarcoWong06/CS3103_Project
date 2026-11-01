Q1 sample configurations

configurations.txt is a table for editing macros, not program input.
Use the following mapping in problem1.c:
  P -> NUM_PRODUCERS       W -> NUM_WORKERS       C -> NUM_CONSUMERS
  K -> ITEMS_PER_PRODUCER  N -> BUFFER_A_SIZE     M -> BUFFER_B_SIZE

For each row, edit all six macros, run make problem1, then run ./problem1.
The document row is the small example in the assignment. The default row
matches the starter. The asymmetric row illustrates independently sized
buffers and different process counts. These are examples, not extra limits.

Check T = P*K ordinary results, with every ID from 0 to T-1 appearing once.
For each item, value = id+1 and the result is its square. Preserve the producer
identity and record the worker that processed the item. Worker IDs can differ
across runs. Print the final results in ascending item ID order, and check
termination and child exit statuses as described in the specification.

Repeat runs and vary workloads and stage speeds as described in Q1. A test
delay can model active work; do not use it to determine execution order or
replace synchronization. These samples do not supply a termination protocol.
