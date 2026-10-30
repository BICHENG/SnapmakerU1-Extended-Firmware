"""Run on Linux to exercise the service's PID ownership guard."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.name == 'posix' and Path('/proc').is_dir(), 'Linux init check')
class InitTests(unittest.TestCase):
    def test_stale_pidfile_does_not_stop_an_unrelated_process(self):
        source = Path(__file__).resolve().parents[1] / 'root/etc/init.d/S99auxmic'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pidfile = root / 'service.pid'
            script = root / 'init'
            script.write_text(source.read_text().replace(
                'PIDFILE=/run/auxmic.pid', 'PIDFILE=' + shlex.quote(str(pidfile))).replace(
                '. /usr/local/share/auxmic/common.sh', ':'))
            process = subprocess.Popen(['sleep', '30'])
            try:
                pidfile.write_text(str(process.pid))
                result = subprocess.run(['sh', str(script), 'stop'], capture_output=True, timeout=3)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIsNone(process.poll(), 'unrelated process was signalled')
                self.assertFalse(pidfile.exists())
            finally:
                if process.poll() is None:
                    process.terminate()
                process.wait()


if __name__ == '__main__':
    unittest.main()
