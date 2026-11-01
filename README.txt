CS3103 Operating Systems Project

Start with CS3103_Project_Specification.docx. It defines the assignment and
grading requirements. If a sample or starter differs from it, follow the
specification. The supplied examples add no grading requirements.

Package contents
  CS3103_Project_Specification.docx  Assignment and grading tables
  problem1.c                       Q1 student starter
  problem2.c                       Q2 student starter
  Makefile                         Linux build commands
  samples/                         Small input/output examples
  test_cases/q1/                   Macro configurations and testing notes
  test_cases/q2/                   Mini-file-system command scripts
  test_cases/q3/                   Scheduling input files

The two C files are incomplete starters. Complete their TODOs. Q1 currently
prints TODO diagnostics and returns a failure status because verification
has not been implemented. Q2 currently prints TODO messages for file-system
operations. Successful compilation does not mean those operations work.

Q3 has no supplied source skeleton, as stated in the specification. Create
problem3.c and implement the required signatures from the document. MLFQ is
optional. No helper module is needed to compile the supplied Q1/Q2 starters.

Build on Linux
  make

Initially this builds problem1 and minifs. Once problem3.c exists, make also
builds problem3. Update the Makefile if you add source files or headers.
The compiler warning flags in this example are build aids.

Equivalent individual build commands:
  gcc -std=gnu11 -Wall -Wextra -pthread problem1.c -o problem1
  gcc -std=c11 -Wall -Wextra problem2.c -o minifs
  gcc -std=c11 -Wall -Wextra problem3.c -o problem3

Run Q1
  ./problem1

Edit the six macros in problem1.c and recompile to change the configuration.
Q1 does not read the old configuration files or command-line parameters.
See test_cases/q1/README.txt for the macro mapping and sample settings.

Run Q2
  ./minifs

Example commands:
  format
  create hello.txt
  write hello.txt "Operating Systems"
  read hello.txt
  exit

The surrounding double quotes are not stored as file content. ls takes no
argument and lists the current directory. See test_cases/q2/README.txt for
scripts and the two-session persistence example.

The name and parent_id fields in problem2.c illustrate one way to store a
directory hierarchy. Save the relationships inside virtual_disk.bin. You may
change the structures and disk layout as allowed by the specification.

Run Q3 after implementing it
  ./problem3 test_cases/q3/case1.txt

Each supplied input starts with the header shown in the specification.
One invocation runs FCFS, SJF, preemptive Priority, RR q=3, and RR q=6 in that
order. There is no extra quantum argument. See test_cases/q3/README.txt.

Testing and submission
The samples illustrate the written requirements; they are not an exhaustive
test suite. Follow each problem's testing and report sections, and the common
submission section. Submit your completed sources, build instructions, tests,
report and executables under the names specified in the document.
See samples/README.txt for worked input/output examples for all three problems.
