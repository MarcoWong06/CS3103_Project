Sample input and output

These examples show the expected behaviour after implementing the TODOs.
The supplied Q1/Q2 starters do not yet produce these results. Examples do
not add requirements beyond the specification. Spacing, prompts, labels and
rounding can differ while preserving the required information and values.

Q1
  q1_configuration.txt shows the six macros for the document's small example.
  Edit problem1.c to use these values, then run:
    make problem1
    ./problem1
  Compare the result rows with q1_output.txt. Q1 has no stdin input file.

Q2
  Run the example in a working directory where you can initialize the disk:
    ./minifs < samples/q2_input.txt
  q2_read_output.txt contains the output of the two read commands only.
  Startup messages, prompts and any success messages are omitted because
  their exact wording is not specified. The outer quotes are not stored.
  For persistence across two invocations, use the two case4 scripts under
  test_cases/q2/ and their README.

Q3
  After implementing problem3.c, build it and run:
    make problem3
    ./problem3 samples/q3_input.txt
  q3_output.txt shows all five required configurations, with per-job rows
  in input order. Its averages are rounded to two decimal places.
  No job arrives exactly at an RR slice boundary in this example, so the
  sample does not choose an extra rule for simultaneous events.
