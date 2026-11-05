Q2 sample command scripts

Run from the package directory, for example:
  ./minifs < test_cases/q2/case1_basic.txt

Scripts use only commands in the specification. Each independent case starts
with format, which initializes the simulated disk in the working directory.
The restart half of case4 is deliberately different: run both halves from
the same directory and do not format or delete virtual_disk.bin between them.

case1_basic.txt
  Exercise create, write, read, append, replacement, deletion, ls and df.
  The first three reads of hello.txt show, in order:
    Operating Systems
    Operating Systems project
    OS
  reused.txt contains Reused.

case2_paths.txt
  Exercise nested directories, absolute/relative paths, '.', '..', and cleanup.
  pwd first shows /courses/os/labs, then /courses/os. The report reads all
  return Path example. The script returns to / before deleting directories.

case3_blocks.txt
  Exercise an empty file, 256-byte content, append across a block boundary,
  600-byte content, shrinkage, deletion and reuse. Content sizes are 0, 256,
  257, 600, then 5 bytes before deletion. The final reused.txt is 600 bytes.
  Check stat and df against the data and metadata layout you document.

case4_persist_create.txt and case4_persist_restart.txt
  Run these as two separate program invocations, in order:
    ./minifs < test_cases/q2/case4_persist_create.txt
    ./minifs < test_cases/q2/case4_persist_restart.txt
  The hierarchy and Operating Systems content must survive restart. The
  restart script then appends text and reads Operating Systems project.

case5_invalid.txt
  After setup, attempt a duplicate name, missing paths, traversal through a
  regular file, removal of a nonempty directory or root, a 32-character name,
  and a write missing the documented quotes. These operations should report
  errors without changing the file system. kept.txt must remain unchanged.
  No exact wording for error messages is prescribed by these samples.

case6_entry_limit.txt
  Root uses one of the 128 metadata slots. Starting from format, the first
  127 creates fit, and creating f127.txt should fail without partial changes.
  f126.txt must remain accessible, and no f127.txt entry should be created.

The starters do not implement these operations yet. Use the cases after
completing the relevant TODOs. These examples do not cover the entire testing
section. In particular, test disk exhaustion and a failed multi-block write
using your documented allocation layout and file capacity; verify that a
failed operation leaves existing content, metadata and free-space state intact.

------------------------------------------------------------------------
Additional regression cases (minifs implementation)

Run from the repository root on Linux (for example in Docker):
  bash test_cases/q2/run_tests.sh              all cases
  bash test_cases/q2/run_tests.sh case7_boundaries case12_corrupt
Each case runs in a fresh directory; its output is compared with
test_cases/q2/expected/<case>.out and virtual_disk.bin must stay 1 MiB.

case0_format
  Every command before format on a missing disk reports an error; format; df;
  restart; df; re-format.
case7_boundaries.txt
  31-character name accepted, 32 rejected; ".", ".." and "/" rejected as new
  names; cd .. at the root; cd into a file; rmdir of ., the current
  directory, the root, a non-empty directory and a file; 300-character path
  component; df unchanged by the failures.
case8_deep_paths
  127 nested 31-character directories, pwd of the deepest, entry-table
  exhaustion, stat by the full absolute path.
case9_file_boundaries.txt
  read/write/delete on a directory or missing file fail; empty-file read and
  stat; write "" frees all blocks; spaces and quotes stored byte for byte;
  freed blocks are reused.
case10_append.txt
  append to a missing file or a directory fails; append to an empty file;
  tail fill crossing a block boundary; tail fill that exactly fills a block.
case11_disk_full
  Fill the disk to one free block; oversized write/append fail without
  changing anything; exact fit succeeds; delete frees space; restart shows
  the same state.
case12_corrupt
  Truncated image, bad magic, FAT entry pointing into the metadata region and
  an out-of-range parent_id: each reports an invalid image and rejects
  commands without crashing; format then succeeds.
