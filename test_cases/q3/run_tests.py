from pathlib import Path
import subprocess
import tempfile
import unittest


PROBLEM3 = Path(__file__).resolve().parents[2] / "problem3"


class SchedulingTests(unittest.TestCase):
    def setUp(self):
        workspace = tempfile.TemporaryDirectory()
        self.addCleanup(workspace.cleanup)
        self.directory = Path(workspace.name)

    def simulate(self, rows, *options):
        orders = self.directory / "orders.txt"
        orders.write_text("job_id arrival burst priority\n" + "\n".join(rows) + "\n")
        return self.run_problem3(orders, *options)

    def run_problem3(self, orders, *options):
        return subprocess.run(
            [PROBLEM3, *options, orders], capture_output=True, text=True, timeout=10
        )

    def test_mlfq_demotes_at_quantum_expiry_when_a_job_arrives(self):
        result = self.simulate(["A 0 14 1", "B 0 13 1", "C 16 1 1"], "--mlfq")
        self.assertEqual(result.returncode, 0, result.stderr)

        mlfq = result.stdout.partition("=== MLFQ ===\n")[2]
        completion_times = {
            fields[0]: int(fields[3])
            for fields in map(str.split, mlfq.splitlines())
            if fields and fields[0] in {"A", "B"}
        }
        self.assertEqual(completion_times, {"A": 27, "B": 28})

    def test_unreadable_input_does_not_produce_schedules(self):
        result = self.run_problem3(self.directory)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")
        self.assertIn("error:", result.stderr)

    def test_idle_interval_and_job_named_idle_have_distinct_labels(self):
        result = self.simulate(["IDLE 2 3 1"])
        self.assertEqual(result.returncode, 0, result.stderr)
        gantt_lines = [line for line in result.stdout.splitlines() if line.startswith("Gantt:")]
        labels = [[label.strip() for label in line.split("|")[1:-1]] for line in gantt_lines]
        self.assertEqual(labels, [["IDLE", '"IDLE"']] * 5)

    def test_preserves_a_long_id_on_a_wide_input_line(self):
        job_id = "order-" + "x" * 48
        result = self.simulate([job_id + " " * 512 + "0 1 1"])
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [line.split() for line in result.stdout.splitlines() if line.startswith(job_id)]
        self.assertEqual(rows, [[job_id, "0", "1", "1", "1", "0"]] * 5)

    def test_schedules_more_than_512_jobs(self):
        job_ids = [f"order-{index}" for index in range(513)]
        result = self.simulate([f"{job_id} 0 1 1" for job_id in job_ids], "--mlfq")
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [line.split() for line in result.stdout.splitlines() if line.startswith("order-")]
        completion_times = [(row[0], int(row[3])) for row in rows]
        self.assertEqual(completion_times, list(zip(job_ids, range(1, 514))) * 6)

    def test_headerless_input_preserves_the_job_named_job_id(self):
        orders = self.directory / "orders.txt"
        orders.write_text("job_id 0 1 1\nA 0 2 1\n")
        result = self.run_problem3(orders)
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [line.split() for line in result.stdout.splitlines() if line.startswith("job_id ")]
        self.assertEqual(rows, [["job_id", "0", "1", "1", "1", "0"]] * 5)


if __name__ == "__main__":
    unittest.main()
