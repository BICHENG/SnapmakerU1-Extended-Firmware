import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class MaliStartTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.limit = self.root / 'max_freq'
        self.limit.write_text('900000000\n')
        self.command('config', 'echo true')
        self.command('insmod', 'exit 0')
        source = (Path(__file__).parents[1] / 'root/etc/init.d/S08mali').read_text()
        # Substitute only privileged filesystem boundaries; execute the actual shell logic.
        source = source.replace('/usr/local/bin/extended-config.py', str(self.root / 'config'))
        source = source.replace('/sys/class/devfreq/ff320000.gpu/max_freq', str(self.limit))
        self.script = self.root / 'S08mali'
        self.script.write_text(source)

    def command(self, name, body):
        path = self.root / name
        path.write_text('#!/bin/sh\n' + body + '\n')
        path.chmod(0o755)

    def start(self):
        return subprocess.run(['sh', str(self.script), 'start'], capture_output=True, text=True,
                              env={**os.environ, 'PATH': str(self.root) + os.pathsep + os.environ['PATH']})

    def test_sets_and_confirms_limit(self):
        self.assertEqual(self.start().returncode, 0)
        self.assertEqual(self.limit.read_text(), '400000000\n')

    def test_write_failure_is_reported(self):
        self.limit.unlink()
        self.limit.mkdir()
        result = self.start()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('could not confirm', result.stderr)

    def test_readback_mismatch_is_reported(self):
        self.command('cat', 'echo 500000000')
        result = self.start()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('could not confirm', result.stderr)

    def test_disabled_does_not_change_limit(self):
        self.command('config', 'echo false')
        self.assertEqual(self.start().returncode, 0)
        self.assertEqual(self.limit.read_text(), '900000000\n')


if __name__ == '__main__':
    unittest.main()
