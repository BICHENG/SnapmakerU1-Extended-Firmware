import importlib.util
import json
import pathlib
import subprocess
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("check_capture", HERE / "check_capture.py")
capture_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture_check)


class CaptureCheckTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = pathlib.Path(self.temporary.name) / "results"
        subprocess.run([str(HERE / "test_capture"), str(root)], check=True, stdout=subprocess.DEVNULL)
        self.directory = root / "complete"

    def report_change(self, key, value):
        path = self.directory / "report.json"
        report = json.loads(path.read_text())
        report[key] = value
        path.write_text(json.dumps(report))

    def test_complete_frame(self):
        result = capture_check.check(self.directory)
        self.assertEqual(result["task_counts"], {"fill": 1})
        self.assertEqual(result["descriptor_bytes"], 112)
        self.assertFalse(result["descriptor_pointers_replayable"])

    def test_unknown_binary(self):
        self.report_change("vendor_sha256", "0" * 64)
        with self.assertRaisesRegex(ValueError, "identity"):
            capture_check.check(self.directory)

    def test_unrestored_callback(self):
        self.report_change("callback_restored", False)
        with self.assertRaisesRegex(ValueError, "callback_restored"):
            capture_check.check(self.directory)

    def test_truncated_frame(self):
        (self.directory / "final.bgra").write_bytes(b"short")
        with self.assertRaisesRegex(ValueError, "frame size"):
            capture_check.check(self.directory)

    def test_corrupt_descriptor(self):
        path = self.directory / "descriptors.bin"
        data = bytearray(path.read_bytes())
        data[32] = 48
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "prefix"):
            capture_check.check(self.directory)

    def test_bad_event_times(self):
        self.report_change("last_flush_finish_ns", 1)
        with self.assertRaisesRegex(ValueError, "boundaries"):
            capture_check.check(self.directory)

    def test_failed_capture(self):
        with self.assertRaisesRegex(ValueError, "unknown_task_abi"):
            capture_check.check(self.directory.parent / "unknown-type")


if __name__ == "__main__":
    unittest.main()
